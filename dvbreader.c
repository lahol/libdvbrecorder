#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <sys/poll.h>

#include "dvbreader.h"
#include "dvbrecorder.h"
#include "events.h"
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

#include "descriptors.h"

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

    dvb_si_descriptor_service *service_info;

    DVBStreamStatus status;

    GMutex event_mutex;
    GCond  event_cond;
    GQueue event_queue;

    GThread *event_thread;

    dvbpsi_t *dvbpsi_handles[N_TS_TABLE_TYPES];
    uint16_t dvbpsi_table_pids[N_TS_TABLE_TYPES];
    guint32 dvbpsi_have_pat : 1;
    guint32 dvbpsi_have_pmt : 1;
    guint32 dvbpsi_have_sdt : 1;

    GList *active_pids;
    DVBTuner *tuner;
    GMutex tuner_mutex;

    int tuner_fd;
    int control_pipe_stream[2];
    GThread *data_thread;

    uint8_t pat_packet_count;
    uint8_t *pat_data;
    uint8_t pmt_packet_count;
    uint8_t *pmt_data;
};

struct DVBReaderListener {
    int fd;
    DVBReaderListenerCallback callback;
    gpointer userdata;
    DVBReaderFilterType filter;
    guint32 have_pat    : 1;
    guint32 have_pmt    : 1;
    guint32 write_error : 1;
};

struct DVBPidDescription {
    uint16_t pid;
    DVBReaderFilterType type;
};

void dvb_reader_reset(DVBReader *reader);

void dvb_reader_push_event(DVBReader *reader, DVBRecorderEvent *event);
void dvb_reader_push_event_next(DVBReader *reader, DVBRecorderEvent *event);
DVBRecorderEvent *dvb_reader_pop_event(DVBReader *reader);

gpointer dvb_reader_event_thread_proc(DVBReader *reader);
gpointer dvb_reader_data_thread_proc(DVBReader *reader);

void dvb_reader_rewrite_pat(DVBReader *reader, uint16_t ts_id, uint16_t program_number, uint16_t program_map_pid);
void dvb_reader_rewrite_pmt(DVBReader *reader, dvbpsi_pmt_t *pmt);
void dvb_reader_listener_send_pat(DVBReader *reader, struct DVBReaderListener *listener);
void dvb_reader_listener_send_pmt(DVBReader *reader, struct DVBReaderListener *listener);

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

    reader->control_pipe_stream[0] = -1;
    reader->control_pipe_stream[1] = -1;

    dvb_reader_reset(reader);

    reader->tuner = dvb_tuner_new(0);
    if (!reader->tuner)
        goto err;

    reader->event_thread = g_thread_new("EventThread", (GThreadFunc)dvb_reader_event_thread_proc, reader);

    return reader;
err:
    dvb_reader_destroy(reader);
    return NULL;
}

void dvb_reader_reset(DVBReader *reader)
{
    if (!reader)
        return;

    dvb_si_descriptor_free((dvb_si_descriptor *)reader->service_info);
    reader->service_info = NULL;

    reader->status = DVB_RECORD_STATUS_UNKNOWN;

    if (reader->dvbpsi_handles[TS_TABLE_PAT] && reader->dvbpsi_handles[TS_TABLE_PAT]->p_decoder)
        dvbpsi_pat_detach(reader->dvbpsi_handles[TS_TABLE_PAT]);
    if (reader->dvbpsi_handles[TS_TABLE_PMT] && reader->dvbpsi_handles[TS_TABLE_PMT]->p_decoder)
        dvbpsi_pmt_detach(reader->dvbpsi_handles[TS_TABLE_PMT]);
    if (reader->dvbpsi_handles[TS_TABLE_EIT] && reader->dvbpsi_handles[TS_TABLE_EIT]->p_decoder)
        dvbpsi_DetachDemux(reader->dvbpsi_handles[TS_TABLE_EIT]);
    if (reader->dvbpsi_handles[TS_TABLE_SDT] && reader->dvbpsi_handles[TS_TABLE_SDT]->p_decoder)
        dvbpsi_DetachDemux(reader->dvbpsi_handles[TS_TABLE_SDT]);
    if (reader->dvbpsi_handles[TS_TABLE_RST] && reader->dvbpsi_handles[TS_TABLE_RST]->p_decoder)
        dvbpsi_rst_detach(reader->dvbpsi_handles[TS_TABLE_RST]);

    int i;
    for (i = 0; i < N_TS_TABLE_TYPES; ++i) {
        if (reader->dvbpsi_handles[i]) {
            dvbpsi_delete(reader->dvbpsi_handles[i]);
            reader->dvbpsi_handles[i] = NULL;
        }
    }

    reader->dvbpsi_table_pids[TS_TABLE_PAT] = 0;
    reader->dvbpsi_table_pids[TS_TABLE_PMT] = 0xffff;
    reader->dvbpsi_table_pids[TS_TABLE_EIT] = 18;
    reader->dvbpsi_table_pids[TS_TABLE_SDT] = 17;
    reader->dvbpsi_table_pids[TS_TABLE_RST] = 19;

    reader->dvbpsi_have_pat = 0;
    reader->dvbpsi_have_pmt = 0;
    reader->dvbpsi_have_sdt = 0;

    /* tuner_fd */
    reader->tuner_fd = -1;

    reader->pat_packet_count = 0;
    reader->pmt_packet_count = 0;
    g_free(reader->pat_data);
    g_free(reader->pmt_data);
    reader->pat_data = NULL;
    reader->pmt_data = NULL;

    GList *tmp;
    struct DVBReaderListener *listener;
    for (tmp = reader->listeners; tmp; tmp = g_list_next(tmp)) {
        listener = (struct DVBReaderListener *)tmp->data;
        if (listener) {
            listener->have_pat = 0;
            listener->have_pmt = 0;
        }
    }
}

void dvb_reader_destroy(DVBReader *reader)
{
    if (!reader)
        return;

    dvb_reader_stop(reader);

    DVBRecorderEvent *quit_event = dvb_recorder_event_new(DVB_RECORDER_EVENT_STOP_THREAD, NULL, NULL);
    dvb_reader_push_event_next(reader, quit_event);

    if (reader->event_thread) {
        g_thread_join(reader->event_thread);
        reader->event_thread = NULL;
    }


    g_queue_free_full(&reader->event_queue, (GDestroyNotify)dvb_recorder_event_destroy);

    g_list_free_full(reader->listeners, g_free);
    g_list_free_full(reader->active_pids, g_free);
}

DVBStreamStatus dvb_reader_get_stream_status(DVBReader *reader)
{
    if (reader == NULL)
        return DVB_STREAM_STATUS_UNKNOWN;
    return reader->status;
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

static gint dvb_reader_compare_listener_cb(struct DVBReaderListener *listener, gpointer callback)
{
    if (listener == NULL)
        return -1;
    if (listener->callback == callback)
        return 0;
    return 1;
}

void dvb_reader_set_listener(DVBReader *reader, DVBReaderFilterType filter, int fd,
                             DVBReaderListenerCallback callback, gpointer userdata)
{
    g_return_if_fail(reader != NULL);

    g_mutex_lock(&reader->listener_mutex);

    GList *element = NULL;
    if (fd >= 0) 
        element = g_list_find_custom(reader->listeners, GINT_TO_POINTER(fd), (GCompareFunc)dvb_reader_compare_listener_fd);
    else
        element = g_list_find_custom(reader->listeners, callback, (GCompareFunc)dvb_reader_compare_listener_cb);

    fprintf(stderr, "[lib] set listener: %d\n", fd);

    if (element) {
        ((struct DVBReaderListener *)element->data)->filter = filter;
        ((struct DVBReaderListener *)element->data)->userdata = userdata;
    }
    else {
        struct DVBReaderListener *listener = g_malloc0(sizeof(struct DVBReaderListener));

        listener->fd = fd;
        listener->callback = callback;
        listener->userdata = userdata;
        listener->filter = filter;

        reader->listeners = g_list_prepend(reader->listeners, listener);
    }

    g_mutex_unlock(&reader->listener_mutex);
}

void dvb_reader_remove_listener(DVBReader *reader, int fd, DVBReaderListenerCallback callback)
{
    g_return_if_fail(reader != NULL);

    g_mutex_lock(&reader->listener_mutex);

    GList *element = NULL;
    if (fd < 0)
        element = g_list_find_custom(reader->listeners, callback, (GCompareFunc)dvb_reader_compare_listener_cb);
    else
        element = g_list_find_custom(reader->listeners, GINT_TO_POINTER(fd), (GCompareFunc)dvb_reader_compare_listener_fd);

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
    fprintf(stderr, "dvb_reader_tune: frequency: %" PRIu32 "\n", frequency);

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

    reader->status = DVB_STREAM_STATUS_RUNNING;

    reader->data_thread = g_thread_new("DataThread", (GThreadFunc)dvb_reader_data_thread_proc, reader);
}

void dvb_reader_stop(DVBReader *reader)
{
    g_return_if_fail(reader != NULL);

    reader->status = DVB_STREAM_STATUS_STOPPED;
    fprintf(stderr, "[lib] dvb_reader_stop: pipe_stream: %d/%d\n", reader->control_pipe_stream[0],
        reader->control_pipe_stream[1]);

    if (reader->control_pipe_stream[1] >= 0) {
        write(reader->control_pipe_stream[1], "quit", 4);
    }

    if (reader->data_thread) {
        g_thread_join(reader->data_thread);
        reader->data_thread = NULL;
    }

    if (reader->control_pipe_stream[1] >= 0) {
        close(reader->control_pipe_stream[1]);
        reader->control_pipe_stream[0] = -1;
    }
    if (reader->control_pipe_stream[0] >= 0) {
        close(reader->control_pipe_stream[0]);
        reader->control_pipe_stream[1] = -1;
    }

    dvb_reader_reset(reader);
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
    fprintf(stderr, "[lib] dvb_reader_event_handle_tune_in frequency: %" PRIu32 "\n", event->frequency);
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

    fprintf(stderr, "[lib]: rc: %d, tuner fd: %d\n", rc, reader->tuner_fd);

    /* FIXME: notify callback about status change */
    if (reader->tuner_fd < 0) {
        dvb_recorder_event_send(DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED,
                reader->event_cb, reader->event_data,
                "status", DVB_STREAM_STATUS_TUNE_FAILED,
                NULL, NULL);
        return;
    }

    dvb_reader_start(reader);

    dvb_recorder_event_send(DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED,
            reader->event_cb, reader->event_data,
            "status", DVB_STREAM_STATUS_TUNED,
            NULL, NULL);
    dvb_recorder_event_send(DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED,
            reader->event_cb, reader->event_data,
            "status", DVB_STREAM_STATUS_RUNNING,
            NULL, NULL);
}

gpointer dvb_reader_event_thread_proc(DVBReader *reader)
{
    DVBRecorderEvent *event;

    while (1) {
        event = dvb_reader_pop_event(reader);

        fprintf(stderr, "[lib] reader_event_thread_proc event: %d\n", event->type);

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
    dvb_reader_add_active_pid(reader, 0, DVB_FILTER_PAT);

    reader->dvbpsi_handles[TS_TABLE_EIT] = dvbpsi_new(dvb_reader_dvbpsi_message, DVBPSI_MSG_WARN);
    dvbpsi_AttachDemux(reader->dvbpsi_handles[TS_TABLE_EIT], dvb_reader_dvbpsi_demux_new_subtable, reader);
    dvb_reader_add_active_pid(reader, 18, DVB_FILTER_EIT);

    reader->dvbpsi_handles[TS_TABLE_SDT] = dvbpsi_new(dvb_reader_dvbpsi_message, DVBPSI_MSG_WARN);
    dvbpsi_AttachDemux(reader->dvbpsi_handles[TS_TABLE_SDT], dvb_reader_dvbpsi_demux_new_subtable, reader);
    dvb_reader_add_active_pid(reader, 17, DVB_FILTER_SDT);

    reader->dvbpsi_handles[TS_TABLE_RST] = dvbpsi_new(dvb_reader_dvbpsi_message, DVBPSI_MSG_WARN);
    dvbpsi_rst_attach(reader->dvbpsi_handles[TS_TABLE_RST], (dvbpsi_rst_callback)dvb_reader_dvbpsi_rst_cb, reader);
    dvb_reader_add_active_pid(reader, 19, DVB_FILTER_RST);


    uint8_t buffer[16384];
    ssize_t bytes_read;
    struct pollfd pfd[2];

    pfd[0].fd = reader->control_pipe_stream[0];
    pfd[0].events = POLLIN;

    fprintf(stderr, "[lib] tuner fd: %d\n", reader->tuner_fd);
    if (reader->tuner_fd < 0) {
        /* send event tuning failed */
        goto done;
    }
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
            if (pfd[0].revents & POLLIN || pfd[0].revents & POLLNVAL) {
                fprintf(stderr, "Received data on control pipe. Stop thread.\n");
                break;
            }
            if (pfd[1].revents & POLLNVAL || pfd[1].revents & POLLHUP || pfd[1].revents & POLLERR) {
                fprintf(stderr, "Input closed\n");
                break;
            }
        }
    }

done:
    ts_reader_free(ts_reader);

    return NULL;
}

gboolean dvb_reader_get_stream_info(DVBReader *reader, gchar **provider, gchar **name, guint8 *type)
{
    g_return_val_if_fail(reader != NULL, FALSE);

    if (reader->service_info == NULL)
        return FALSE;

    if (provider)
        *provider = g_strdup(reader->service_info->provider);
    if (name)
        *name = g_strdup(reader->service_info->name);
    if (type)
        *type = reader->service_info->type;

    return TRUE;
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

GList *dvb_reader_dvbpsi_handle_descriptors(DVBReader *reader, dvbpsi_descriptor_t *desc)
{
    GList *descriptors = NULL;
    dvb_si_descriptor *d;
    while (desc) {
        d = dvb_si_descriptor_decode(desc);
        if (d)
            descriptors = g_list_prepend(descriptors, d);
        desc = desc->p_next;
    }

    return g_list_reverse(descriptors);
}

void dvb_reader_dvbpsi_pat_cb(DVBReader *reader, dvbpsi_pat_t *pat)
{
    fprintf(stderr, "pat_cb\n");
    if (reader->dvbpsi_have_pat) {
        dvbpsi_pat_delete(pat);
        return;
    }

    dvbpsi_pat_program_t *prog;
   
    for (prog = pat->p_first_program; prog; prog = prog->p_next) {
        if (prog->i_number == reader->program_number ) {
            reader->dvbpsi_handles[TS_TABLE_PMT] = dvbpsi_new(dvb_reader_dvbpsi_message, DVBPSI_MSG_WARN);
            dvbpsi_pmt_attach(reader->dvbpsi_handles[TS_TABLE_PMT], reader->program_number,
                    (dvbpsi_pmt_callback)dvb_reader_dvbpsi_pmt_cb, reader);
            reader->dvbpsi_table_pids[TS_TABLE_PMT] = prog->i_pid;
            dvb_reader_add_active_pid(reader, prog->i_pid, DVB_FILTER_PMT);

            dvb_reader_rewrite_pat(reader, pat->i_ts_id, prog->i_number, prog->i_pid);

            break;
        }
    }

    dvbpsi_pat_delete(pat);

    reader->dvbpsi_have_pat = 1;
}

void dvb_reader_dvbpsi_pmt_cb(DVBReader *reader, dvbpsi_pmt_t *pmt)
{
    fprintf(stderr, "pmt_cb\n");
    if (reader->dvbpsi_have_pmt) {
        dvbpsi_pmt_delete(pmt);
        return;
    }

    dvb_reader_rewrite_pmt(reader, pmt);

    dvbpsi_pmt_es_t *stream;
    DVBReaderFilterType type;
    for (stream = pmt->p_first_es; stream; stream = stream->p_next) {
        switch (stream->i_type) {
            case 0x01:
            case 0x02:
                type = DVB_FILTER_VIDEO;
                break;
            case 0x03:
            case 0x04:
                type = DVB_FILTER_AUDIO;
                break;
            case 0x06:
                type = DVB_FILTER_TELETEXT;
                break;
            default:
                type = DVB_FILTER_UNKNOWN;
                break;
        }

        dvb_reader_add_active_pid(reader, stream->i_pid, type);
    }

    dvbpsi_pmt_delete(pmt);

    reader->dvbpsi_have_pmt = 1;
}

void dvb_reader_dvbpsi_eit_cb(DVBReader *reader, dvbpsi_eit_t *eit)
{
    fprintf(stderr, "eit_cb\n");

    dvbpsi_eit_delete(eit);
}

void dvb_reader_dvbpsi_sdt_cb(DVBReader *reader, dvbpsi_sdt_t *sdt)
{
    if (reader->dvbpsi_have_sdt) {
        dvbpsi_sdt_delete(sdt);
        return;
    }
    dvbpsi_sdt_service_t *service;
    GList *desc_list = NULL;
    for (service = sdt->p_first_service; service; service = service->p_next) {
        if (service->i_service_id == reader->program_number) {
            desc_list = dvb_reader_dvbpsi_handle_descriptors(reader, service->p_first_descriptor);
            break;
        }
    }

    GList *tmp;
    for (tmp = desc_list; tmp; tmp = g_list_next(tmp)) {
        if (((dvb_si_descriptor *)tmp->data)->tag == 0x48) { /* service descriptor */
            reader->service_info = (dvb_si_descriptor_service *)tmp->data;
            tmp->data = NULL;
            break;
        }
    }
    
    dvbpsi_sdt_delete(sdt);
    g_list_free_full(desc_list, (GDestroyNotify)dvb_si_descriptor_free);

    reader->dvbpsi_have_sdt = 1;
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

/* Only needed for PAT/PMT, which are limited to 1024 bytes per section. Therefore, at most 6 packets are needed
 * and an uint8_t suffices for the packet count. */
void dvb_reader_dvbpsi_section_to_ts_packets(uint16_t pid, dvbpsi_psi_section_t *section, uint8_t **packets, uint8_t *packet_count)
{
    if (section == NULL) {
        if (packets) *packets = NULL;
        if (packet_count) *packet_count = 0;
        return;
    }

    uint16_t size = (uint16_t)(section->p_payload_end - section->p_data);
    /* p_payload_end is first byte of crc32 */
    if (dvbpsi_has_CRC32(section))
        size += 4;

    uint8_t count = (size + 184) / 184; /* ceil(x/y) = (x+y-1)/y for positive integers; first package
                                         * only has 183 bytes + pointer field, we get
                                         * count = (size + 1 + 184 - 1)/184. */
    uint8_t *buffer = g_malloc(TS_SIZE * count);
    uint8_t *current = buffer;
    uint8_t *pdata = section->p_data;   /* contains raw section data, including crc32 */
    uint8_t *pdata_end = section->p_payload_end;
    if (dvbpsi_has_CRC32(section))
        pdata_end += 4;

    gboolean first = TRUE;
    uint16_t left_to_write = pdata_end - pdata;

    uint8_t i;
    for (i = 0; i < count; ++i) {
        ts_init(current);
        ts_set_pid(current, pid);
        ts_set_payload(current);
        ts_set_cc(current, i);
        
        if (G_UNLIKELY(first)) {
            ts_set_unitstart(current);
            current[4] = 0x00; /* pointer */
            current += 5;
            if (left_to_write < 183) {
                memcpy(current, pdata, left_to_write);
                pdata += left_to_write;
                current += left_to_write;
                left_to_write = 0;
                break;
            }
            else {
                memcpy(current, pdata, 183);
                pdata += 183;
                current += 183;
                left_to_write -= 183;
            }
            first = FALSE;
        }
        else {
            current += 4;
            if (left_to_write < 184) {
                memcpy(current, pdata, left_to_write);
                pdata += left_to_write;
                current += left_to_write;
                left_to_write = 0;
                break;
            }
            else {
                memcpy(current, pdata, 184);
                pdata += 184;
                current += 184;
                left_to_write -= 184;
            }
        }
    }

    /* fill rest of buffer */
    uint8_t *buffer_end = buffer + TS_SIZE * count;
    for ( ; current < buffer_end; ++current)
        *current = 0xff;

    if (packets)
        *packets = buffer;
    if (packet_count)
        *packet_count = count;
}

void dvb_reader_rewrite_pat(DVBReader *reader, uint16_t ts_id, uint16_t program_number, uint16_t program_map_pid)
{
    /* have only one pat packet */
    g_free(reader->pat_data);

    dvbpsi_t *encoder_handle = dvbpsi_new(dvb_reader_dvbpsi_message, DVBPSI_MSG_WARN);
    dvbpsi_pat_t *pat = dvbpsi_pat_new(ts_id, 0, true);
    dvbpsi_pat_program_add(pat, program_number, program_map_pid);

    dvbpsi_psi_section_t *section = dvbpsi_pat_sections_generate(encoder_handle, pat, 0);
    dvb_reader_dvbpsi_section_to_ts_packets(0, section, &reader->pat_data, &reader->pat_packet_count);

    dvbpsi_DeletePSISections(section);
    dvbpsi_pat_delete(pat);
    dvbpsi_delete(encoder_handle);
}

void dvb_reader_rewrite_pmt(DVBReader *reader, dvbpsi_pmt_t *pmt)
{
    g_free(reader->pmt_data);

    dvbpsi_t *encoder_handle = dvbpsi_new(dvb_reader_dvbpsi_message, DVBPSI_MSG_WARN);

    /* FIXME: handle multiple sections? */
    dvbpsi_psi_section_t *section = dvbpsi_pmt_sections_generate(encoder_handle, pmt);
    dvb_reader_dvbpsi_section_to_ts_packets(reader->dvbpsi_table_pids[TS_TABLE_PMT],
                                            section, &reader->pmt_data, &reader->pmt_packet_count);

    dvbpsi_DeletePSISections(section);
    dvbpsi_delete(encoder_handle);
}

gboolean _dvb_reader_write_packet_full(int fd, const uint8_t *packet)
{
    ssize_t nw, offset;
    for (offset = 0; offset < TS_SIZE; offset += nw) {
        if ((nw = write(fd, packet + offset, (size_t)(TS_SIZE - offset))) <= 0) {
            if (nw < 0) {
                fprintf(stderr, "Could not write.\n");
                return FALSE;
            }
            break;
        }
    }

    return TRUE;
}

void _dump_packet(const uint8_t *packet)
{
    uint16_t i;
    for (i = 0; i < TS_SIZE; ++i) {
        fprintf(stderr, "%02x ", packet[i]);
        if (i % 16 == 15)
            fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

void dvb_reader_listener_send_pat(DVBReader *reader, struct DVBReaderListener *listener)
{
    if (reader->pat_packet_count == 0)
        return;

    uint8_t i;
    for (i = 0; i < reader->pat_packet_count; ++i) {
        fprintf(stderr, "PAT[%d]:\n", i);
        _dump_packet(&reader->pat_data[i * TS_SIZE]);
        listener->have_pat = 1;
        if (listener->fd >= 0) {
            if (!_dvb_reader_write_packet_full(listener->fd, &reader->pat_data[i * TS_SIZE]))
                listener->have_pat = 0;
        }
        if (listener->callback) {
            listener->callback(&reader->pat_data[i * TS_SIZE], DVB_FILTER_PAT, listener->userdata);
        }
    }
}

void dvb_reader_listener_send_pmt(DVBReader *reader, struct DVBReaderListener *listener)
{
    if (reader->pmt_packet_count == 0)
        return;

    uint8_t i;
    for (i = 0; i < reader->pmt_packet_count; ++i) {
        fprintf(stderr, "PMT[%d]:\n", i);
        _dump_packet(&reader->pmt_data[i * TS_SIZE]);
        listener->have_pmt = 1;
        if (listener->fd >= 0) {
            if (!_dvb_reader_write_packet_full(listener->fd, &reader->pmt_data[i * TS_SIZE]))
                listener->have_pat = 0;
        }
        if (listener->callback) {
            listener->callback(&reader->pmt_data[i * TS_SIZE], DVB_FILTER_PMT, listener->userdata);
        }
    }
}

gboolean dvb_reader_get_current_pat_packets(DVBReader *reader, guint8 **buffer, gsize *length)
{
    g_return_val_if_fail(reader != NULL, FALSE);

    if (reader->pat_packet_count == 0)
        return FALSE;

    if (buffer) {
        *buffer = g_malloc(reader->pat_packet_count * TS_SIZE);
        memcpy(*buffer, reader->pat_data, reader->pat_packet_count * TS_SIZE);
    }
    if (length)
        *length = (gsize)(reader->pat_packet_count * TS_SIZE);

    return TRUE;
}

gboolean dvb_reader_get_current_pmt_packets(DVBReader *reader, guint8 **buffer, gsize *length)
{
    g_return_val_if_fail(reader != NULL, FALSE);

    if (reader->pmt_packet_count == 0)
        return FALSE;

    if (buffer) {
        *buffer = g_malloc(reader->pmt_packet_count * TS_SIZE);
        memcpy(*buffer, reader->pmt_data, reader->pmt_packet_count * TS_SIZE);
    }
    if (length)
        *length = (gsize)(reader->pmt_packet_count * TS_SIZE);

    return TRUE;
}

gboolean dvb_reader_write_packet(DVBReader *reader, const uint8_t *packet)
{
    GList *tmp;
    struct DVBReaderListener *listener;
    DVBReaderFilterType type = dvb_reader_get_active_pid_type(reader, ts_get_pid(packet));

    g_mutex_lock(&reader->listener_mutex);
    for (tmp = reader->listeners; tmp; tmp = g_list_next(tmp)) {
        listener = (struct DVBReaderListener *)tmp->data;
        if ((listener->filter & (DVB_FILTER_ALL & ~(DVB_FILTER_PAT | DVB_FILTER_PMT))) & type) {
            if (listener->fd >= 0 && !listener->write_error) {
                if (!_dvb_reader_write_packet_full(listener->fd, packet)) {
                    listener->write_error = 1;
                    break;
                }
            }
            if (listener->callback) {
                listener->callback(packet, type, listener->userdata);
            }
        }
        else if (type == DVB_FILTER_PAT && (listener->filter & DVB_FILTER_PAT) && listener->have_pat == 0) {
            dvb_reader_listener_send_pat(reader, listener);
        }
        else if (type == DVB_FILTER_PMT && (listener->filter & DVB_FILTER_PMT) && listener->have_pmt == 0) {
            dvb_reader_listener_send_pmt(reader, listener);
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

