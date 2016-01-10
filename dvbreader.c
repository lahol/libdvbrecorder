#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/poll.h>

#include "dvbreader.h"
#include "dvbrecorder.h"
#include "dvbrecorder-event.h"
#include "read-ts.h"
#include "dvb-tuner.h"

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/demux.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/pat.h>
#include <dvbpsi/pmt.h>
#include <dvbpsi/eit.h>
#include <dvbpsi/sdt.h>
#include <dvbpsi/rst.h>

struct _DVBReader {
    DVBRecorderEventCallback event_cb;
    gpointer event_data;

    GList *listeners;
    GMutex listener_mutex;

    guint32 frequency;
    guint8  polarization;
    guint8  sat_no;
    guint32 symbol_rate;
    guint16 program_number;

    guint8  status;

    GMutex event_mutex;
    GCond  event_cond;
    GQueue event_queue;

    GThread *event_thread;

    dvbpsi_t *dvbpsi_handles[N_TS_TABLE_TYPES];
    uint16_t dvbpsi_table_pids[N_TS_TABLE_TYPES];

    GList *active_pids;
    DVBTuner *tuner;
    GMutex tuner_mutex;

    int tuner_fd;
    int control_pipe_stream[2];
    GThread *data_thread;
};

struct DVBReaderListener {
    int fd;
    DVBReaderFilterType filter;
    guint32 write_error : 1;
};

struct DVBPidDescription {
    uint16_t pid;
    DVBReaderFilterType type;
};

void dvb_reader_push_event(DVBReader *reader, DVBRecorderEvent *event);
void dvb_reader_push_event_next(DVBReader *reader, DVBRecorderEvent *event);
DVBRecorderEvent *dvb_reader_pop_event(DVBReader *reader);

gpointer dvb_reader_event_thread_proc(DVBReader *reader);
gpointer dvb_reader_data_thread_proc(DVBReader *reader);

void dvb_reader_dvbpsi_message(dvbpsi_t *handle, const dvbpsi_msg_level_t level, const char *msg);
void dvb_reader_dvbpsi_pat_cb(DVBReader *reader, dvbpsi_pat_t *pat);
void dvb_reader_dvbpsi_pmt_cb(DVBReader *reader, dvbpsi_pmt_t *pmt);
void dvb_reader_dvbpsi_eit_cb(DVBReader *reader, dvbpsi_eit_t *eit);
void dvb_reader_dvbpsi_sdt_cb(DVBReader *reader, dvbpsi_sdt_t *sdt);
void dvb_reader_dvbpsi_rst_cb(DVBReader *reader, dvbpsi_rst_t *rst);
void dvb_reader_dvbpsi_demux_new_subtable(dvbpsi_t *handle, uint8_t table_id, uint16_t extension, void *userdata);
gboolean dvb_reader_write_packet(DVBReader *reader, const uint8_t *packet);
gboolean dvb_reader_handle_packet(const uint8_t *packet, void *userdata);

DVBReader *dvb_reader_new(DVBRecorderEventCallback cb, gpointer userdata)
{
    DVBReader *reader = g_malloc0(sizeof(DVBReader));

    reader->event_cb = cb;
    reader->event_data = userdata;

    g_mutex_init(&reader->listener_mutex);
    g_mutex_init(&reader->event_mutex);
    g_mutex_init(&reader->tuner_mutex);
    g_cond_init(&reader->event_cond);
    g_queue_init(&reader->event_queue);

    reader->dvbpsi_table_pids[TS_TABLE_PAT] = 0;
    reader->dvbpsi_table_pids[TS_TABLE_PMT] = 0xffff;
    reader->dvbpsi_table_pids[TS_TABLE_EIT] = 18;
    reader->dvbpsi_table_pids[TS_TABLE_SDT] = 17;
    reader->dvbpsi_table_pids[TS_TABLE_RST] = 19;

    reader->control_pipe_stream[0] = -1;
    reader->control_pipe_stream[1] = -1;

    reader->tuner = dvb_tuner_new(0);

    reader->event_thread = g_thread_new("EventThread", (GThreadFunc)dvb_reader_event_thread_proc, reader);

    return reader;
}

void dvb_reader_destroy(DVBReader *reader)
{
    if (!reader)
        return;

    dvb_reader_stop(reader);

    DVBRecorderEvent *quit_event = dvb_recorder_event_new(DVB_RECORDER_EVENT_STOP_THREAD, NULL, NULL);
    dvb_reader_push_event_next(reader, quit_event);

    g_thread_join(reader->event_thread);
    reader->event_thread = NULL;


    g_queue_free_full(&reader->event_queue, (GDestroyNotify)dvb_recorder_event_destroy);

    g_list_free_full(reader->listeners, g_free);
    g_list_free_full(reader->active_pids, g_free);
}

void dvb_reader_add_active_pid(DVBReader *reader, uint16_t pid, DVBReaderFilterType type)
{
    struct DVBPidDescription *desc = g_malloc0(sizeof(struct DVBPidDescription));

    desc->pid = pid;
    desc->type = type;

    g_mutex_lock(&reader->tuner_mutex);
    dvb_tuner_add_pid(reader->tuner, pid);
    g_mutex_unlock(&reader->tuner_mutex);

    reader->active_pids = g_list_prepend(reader->active_pids, desc);
}

DVBReaderFilterType dvb_reader_get_active_pid_type(DVBReader *reader, uint16_t pid)
{
    GList *tmp;
    for (tmp = reader->active_pids; tmp; tmp = g_list_next(tmp)) {
        if (((struct DVBPidDescription *)tmp->data)->pid == pid)
            return ((struct DVBPidDescription *)tmp->data)->type;
    }

    return DVB_FILTER_UNKNOWN;
}

static gint dvb_reader_compare_listener_fd(struct DVBReaderListener *listener, gpointer fd)
{
    if (listener == NULL)
        return -1;
    if (listener->fd == GPOINTER_TO_INT(fd))
        return 0;
    return 1;
}

void dvb_reader_set_listener(DVBReader *reader, int fd, DVBReaderFilterType filter)
{
    g_return_if_fail(reader != NULL);

    g_mutex_lock(&reader->listener_mutex);

    GList *element = g_list_find_custom(reader->listeners, GINT_TO_POINTER(fd), (GCompareFunc)dvb_reader_compare_listener_fd);

    fprintf(stderr, "[lib] set listener: %d\n", fd);

    if (element) {
        ((struct DVBReaderListener *)element->data)->filter = filter;
    }
    else {
        struct DVBReaderListener *listener = g_malloc0(sizeof(struct DVBReaderListener));

        listener->fd = fd;
        listener->filter = filter;

        reader->listeners = g_list_prepend(reader->listeners, listener);
    }

    g_mutex_unlock(&reader->listener_mutex);
}

void dvb_reader_remove_listener(DVBReader *reader, int fd)
{
    g_return_if_fail(reader != NULL);

    g_mutex_lock(&reader->listener_mutex);

    GList *element = g_list_find_custom(reader->listeners, GINT_TO_POINTER(fd), (GCompareFunc)dvb_reader_compare_listener_fd);

    if (element) {
        reader->listeners = g_list_remove_link(reader->listeners, element);
        g_free(element->data);
        g_list_free_1(element);
    }

    g_mutex_unlock(&reader->listener_mutex);
}

void dvb_reader_tune(DVBReader *reader,
                     guint32 frequency,
                     guint8  polarization,
                     guint8  sat_no,
                     guint32 symbol_rate,
                     guint16 program_number)
{
    g_return_if_fail(reader != NULL);

    /* FIXME: stop running stream first */
    DVBRecorderEvent *event = dvb_recorder_event_new(DVB_RECORDER_EVENT_TUNE_IN,
                                                     "frequency", frequency,
                                                     "polarization", polarization,
                                                     "sat_no", sat_no,
                                                     "symbol_rate", symbol_rate,
                                                     "program_number", program_number,
                                                     NULL, NULL);
    dvb_reader_push_event(reader, event);
}

void dvb_reader_start(DVBReader *reader)
{
    g_return_if_fail(reader != NULL);

    if (pipe(reader->control_pipe_stream) != 0)
        fprintf(stderr, "Error creating control pipe.\n");

    reader->data_thread = g_thread_new("DataThread", (GThreadFunc)dvb_reader_data_thread_proc, reader);
}

void dvb_reader_stop(DVBReader *reader)
{
    g_return_if_fail(reader != NULL);

    if (reader->control_pipe_stream[1] >= 0) {
        write(reader->control_pipe_stream[1], "quit", 4);
        close(reader->control_pipe_stream[0]);
        close(reader->control_pipe_stream[1]);
    }

    if (reader->data_thread) {
        g_thread_join(reader->data_thread);
        reader->data_thread = NULL;
    }

    reader->control_pipe_stream[0] = -1;
    reader->control_pipe_stream[1] = -1;
}

void dvb_reader_push_event(DVBReader *reader, DVBRecorderEvent *event)
{
    g_mutex_lock(&reader->event_mutex);

    g_queue_push_tail(&reader->event_queue, event);
    g_cond_signal(&reader->event_cond);
    
    g_mutex_unlock(&reader->event_mutex);
}

void dvb_reader_push_event_next(DVBReader *reader, DVBRecorderEvent *event)
{
    g_mutex_lock(&reader->event_mutex);

    g_queue_push_head(&reader->event_queue, event);
    g_cond_signal(&reader->event_cond);
    
    g_mutex_unlock(&reader->event_mutex);
}

DVBRecorderEvent *dvb_reader_pop_event(DVBReader *reader)
{
    DVBRecorderEvent *event;

    g_mutex_lock(&reader->event_mutex);

    while ((event = (DVBRecorderEvent *)g_queue_pop_head(&reader->event_queue)) == NULL)
        g_cond_wait(&reader->event_cond, &reader->event_mutex);

    g_mutex_unlock(&reader->event_mutex);

    return event;
}

void dvb_reader_event_handle_tune_in(DVBReader *reader, DVBRecorderEventTuneIn *event)
{
    fprintf(stderr, "Tune In Handler\n");
    dvb_reader_stop(reader);
    int rc;
    g_mutex_lock(&reader->tuner_mutex);
    /* FIXME: make this cancellable */
    rc = dvb_tuner_tune(reader->tuner, event->frequency, event->polarization, event->sat_no,
                                       event->symbol_rate, NULL, 0);
    if (rc == 0) {
        reader->frequency = event->frequency;
        reader->polarization = event->polarization;
        reader->sat_no = event->sat_no;
        reader->symbol_rate = event->symbol_rate;
        reader->program_number = event->program_number;
    }

    reader->tuner_fd = dvb_tuner_get_fd(reader->tuner);
    g_mutex_unlock(&reader->tuner_mutex);

    dvb_reader_add_active_pid(reader, 0, DVB_FILTER_PAT);

    /* FIXME: notify callback about status change */
    /* wait in client for tune in and then (if ready) start stream */
}

gpointer dvb_reader_event_thread_proc(DVBReader *reader)
{
    DVBRecorderEvent *event;

    while (1) {
        event = dvb_reader_pop_event(reader);

        fprintf(stderr, "event: %d\n", event->type);

        switch (event->type) {
            case DVB_RECORDER_EVENT_STOP_THREAD:
                dvb_recorder_event_destroy(event);
                return NULL;
            case DVB_RECORDER_EVENT_TUNE_IN:
                dvb_reader_event_handle_tune_in(reader, (DVBRecorderEventTuneIn *)event);
                break;
            default:
                break;
        }

        dvb_recorder_event_destroy(event);
    }

    return NULL;
}

gpointer dvb_reader_data_thread_proc(DVBReader *reader)
{
    fprintf(stderr, "[lib] dvb_reader_data_thread_proc\n");
    static TsReaderClass tscls = {
        .handle_packet = dvb_reader_handle_packet,
    };
    TsReader *ts_reader = ts_reader_new(&tscls, reader);

    reader->dvbpsi_handles[TS_TABLE_PAT] = dvbpsi_new(dvb_reader_dvbpsi_message, DVBPSI_MSG_WARN);
    dvbpsi_pat_attach(reader->dvbpsi_handles[TS_TABLE_PAT], (dvbpsi_pat_callback)dvb_reader_dvbpsi_pat_cb, reader);

    uint8_t buffer[16384];
    ssize_t bytes_read;
    struct pollfd pfd[2]; /* FIXME: control pipe */

    pfd[0].fd = reader->control_pipe_stream[0];
    pfd[0].events = POLLIN;

    fprintf(stderr, "[lib] tuner fd: %d\n", reader->tuner_fd);
    pfd[1].fd = reader->tuner_fd;
    pfd[1].events = POLLIN;

    while (1) {
        if (poll(pfd, 2, 15000)) {
            if (pfd[1].revents & POLLIN) {
                bytes_read = read(pfd[1].fd, buffer, 16384);
                if (bytes_read <= 0) {
                    if (errno == EAGAIN)
                        continue;
                    fprintf(stderr, "[lib] Error reading data. Stopping thread. (%d) %s\n", errno, strerror(errno));
                    break;
                }
                ts_reader_push_buffer(ts_reader, buffer, bytes_read);
            }
            else if (pfd[0].revents & POLLIN) {
                fprintf(stderr, "Received data on control pipe. Stop thread.\n");
                break;
            }
        }
    }

    ts_reader_free(ts_reader);

    return NULL;
}

/************************************************
 *
 *  TS handling
 *
 ************************************************/

void dvb_reader_dvbpsi_message(dvbpsi_t *handle, const dvbpsi_msg_level_t level, const char *msg)
{
    switch (level) {
        case DVBPSI_MSG_ERROR: fprintf(stderr, "[DVBPSI Error] "); break;
        case DVBPSI_MSG_WARN:  fprintf(stderr, "[DVBPSI Warning] "); break;
        case DVBPSI_MSG_DEBUG: fprintf(stderr, "[DVBPSI Debug] "); break;
        default:
            break;
    }
    fprintf(stderr, "%s\n", msg);
}

void dvb_reader_dvbpsi_pat_cb(DVBReader *reader, dvbpsi_pat_t *pat)
{
    fprintf(stderr, "pat_cb\n");

    dvbpsi_pat_delete(pat);
}

void dvb_reader_dvbpsi_pmt_cb(DVBReader *reader, dvbpsi_pmt_t *pmt)
{
    fprintf(stderr, "pmt_cb\n");

    dvbpsi_pmt_delete(pmt);
}

void dvb_reader_dvbpsi_eit_cb(DVBReader *reader, dvbpsi_eit_t *eit)
{
    fprintf(stderr, "eit_cb\n");

    dvbpsi_eit_delete(eit);
}

void dvb_reader_dvbpsi_sdt_cb(DVBReader *reader, dvbpsi_sdt_t *sdt)
{
    fprintf(stderr, "sdt_cb\n");

    dvbpsi_sdt_delete(sdt);
}

void dvb_reader_dvbpsi_rst_cb(DVBReader *reader, dvbpsi_rst_t *rst)
{
    fprintf(stderr, "rst_cb\n");

    dvbpsi_rst_delete(rst);
}

void dvb_reader_dvbpsi_demux_new_subtable(dvbpsi_t *handle, uint8_t table_id, uint16_t extension, void *userdata)
{
    if (table_id >= 0x4e || (table_id >= 0x50 && table_id <= 0x5f)) {
        if (extension == ((DVBReader *)userdata)->program_number)
            dvbpsi_eit_attach(handle, table_id, extension, (dvbpsi_eit_callback)dvb_reader_dvbpsi_eit_cb, userdata);
    }
    else if (table_id == 0x42) {
        dvbpsi_sdt_attach(handle, table_id, extension, (dvbpsi_sdt_callback)dvb_reader_dvbpsi_sdt_cb, userdata);
    }
}

gboolean dvb_reader_write_packet(DVBReader *reader, const uint8_t *packet)
{
    ssize_t nw, offset;
    GList *tmp;
    struct DVBReaderListener *listener;
    DVBReaderFilterType type = dvb_reader_get_active_pid_type(reader, ts_get_pid(packet));

    g_mutex_lock(&reader->listener_mutex);
    for (tmp = reader->listeners; tmp; tmp = g_list_next(tmp)) {
        listener = (struct DVBReaderListener *)tmp->data;
        if ((listener->filter & type) && listener->fd >= 0 && !listener->write_error) {
            for (offset = 0; offset < TS_SIZE; offset += nw) {
                if ((nw = write(listener->fd, packet + offset, (size_t)(TS_SIZE - offset))) <= 0) {
                    if (nw < 0) {
                        fprintf(stderr, "Could not write.\n");
                        listener->write_error = 1;
                    }
                    break;
                }
            }
        }
    }
    g_mutex_unlock(&reader->listener_mutex);
    return TRUE;
}

gboolean dvb_reader_handle_packet(const uint8_t *packet, void *userdata)
{
    DVBReader *reader = (DVBReader *)userdata;
    uint16_t pid = ts_get_pid(packet);

    uint8_t i;
    for (i = 0; i < N_TS_TABLE_TYPES; ++i) {
        if (reader->dvbpsi_table_pids[i] == pid) {
            if (reader->dvbpsi_handles[i])
                dvbpsi_packet_push(reader->dvbpsi_handles[i], (uint8_t *)packet);
            break;
        }
    }

    return dvb_reader_write_packet(reader, packet);
}

