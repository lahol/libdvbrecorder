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
#include "epg.h"
#include "epg-internal.h"
#include "logging-internal.h"

struct _DVBReader {
    DVBRecorderEventCallback event_cb;
    gpointer event_data;

    DVBRecorderLogger *logger;

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

    GList *eit_tables;     /* each entry contains a list of events belonging to the same table*/
};

struct EITable {
    guint8 table_id;
    guint8 version;
    GList *events;
};

#define DVB_LISTENER_BUFFER_SIZE 4096

struct DVBReaderListener {
    int fd;
    DVBReaderListenerCallback callback;
    gpointer userdata;

    DVBReader *reader;

    DVBFilterType filter;

    guint32 error_count;
    GQueue message_queue;
    GCond  message_cond;
    GMutex message_lock;
    GThread *worker_thread;

    gsize buffer_size;
    uint8_t buffer[DVB_LISTENER_BUFFER_SIZE];

    guint32 have_pat    : 1;
    guint32 have_pmt    : 1;
    guint32 write_error : 1;
    guint32 eos         : 1;
    guint32 terminate   : 1;
    guint32 running     : 1;
};

enum DVBReaderListenerMessageType {
    DVB_READER_LISTENER_MESSAGE_DATA,
    DVB_READER_LISTENER_MESSAGE_DROP,
    DVB_READER_LISTENER_MESSAGE_QUIT,
    DVB_READER_LISTENER_MESSAGE_EOS,
    DVB_READER_LISTENER_MESSAGE_CONTINUE
};

struct DVBReaderListenerMessage {
    enum DVBReaderListenerMessageType type;
    gsize data_size;
    uint8_t data[DVB_LISTENER_BUFFER_SIZE];
};

struct DVBPidDescription {
    uint16_t pid;
    DVBFilterType type;
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
gpointer dvb_reader_listener_thread_proc(struct DVBReaderListener *listener);
void dvb_reader_listener_send_message(struct DVBReaderListener *listener, enum DVBReaderListenerMessageType type,
                                      uint8_t *data, gsize size, gboolean immediately);
void dvb_reader_listener_broadcast_message(DVBReader *reader, enum DVBReaderListenerMessageType type,
                                           uint8_t *data, gsize size, gboolean immediately);
struct DVBReaderListenerMessage *dvb_reader_listener_pop_message(struct DVBReaderListener *listener);
void dvb_reader_listener_drop_data_messages(struct DVBReaderListener *listener);
void dvb_reader_listener_push_packet(struct DVBReaderListener *listener, DVBFilterType type, const uint8_t *packet);
gint dvb_reader_listener_write_data_full(struct DVBReaderListener *listener, const uint8_t *data, gsize size);
void dvb_reader_listener_clear_queue(struct DVBReaderListener *listener);
void dvb_reader_listener_free(struct DVBReaderListener *listener);

void dvb_reader_dvbpsi_message(dvbpsi_t *handle, const dvbpsi_msg_level_t level, const char *msg);
void dvb_reader_dvbpsi_pat_cb(DVBReader *reader, dvbpsi_pat_t *pat);
void dvb_reader_dvbpsi_pmt_cb(DVBReader *reader, dvbpsi_pmt_t *pmt);
void dvb_reader_dvbpsi_eit_cb(DVBReader *reader, dvbpsi_eit_t *eit);
void dvb_reader_dvbpsi_sdt_cb(DVBReader *reader, dvbpsi_sdt_t *sdt);
void dvb_reader_dvbpsi_rst_cb(DVBReader *reader, dvbpsi_rst_t *rst);
void dvb_reader_dvbpsi_demux_new_subtable(dvbpsi_t *handle, uint8_t table_id, uint16_t extension, void *userdata);
gboolean dvb_reader_write_packet(DVBReader *reader, const uint8_t *packet);
gboolean dvb_reader_handle_packet(const uint8_t *packet, void *userdata);

void dvb_reader_set_logger(DVBReader *reader, DVBRecorderLogger *logger)
{
    FLOG("\n");
    if (!reader)
        return;
    reader->logger = logger;
    dvb_tuner_set_logger(reader->tuner, logger);
}

gint dvb_reader_compare_event_tables_id(struct EITable *a, struct EITable *b)
{
    FLOG("\n");
    if (a == NULL)
        return 1;
    if (b == NULL)
        return -1;
    if (a->table_id < b->table_id)
        return -1;
    else if (a->table_id > b->table_id)
        return 1;
    return 0;
}

gint dvb_reader_find_table_id(struct EITable *table, guint8 table_id)
{
    FLOG("\n");
    if (table && table->table_id == table_id)
        return 0;
    return 1;
}

DVBReader *dvb_reader_new(DVBRecorderEventCallback cb, gpointer userdata)
{
    FLOG("\n");
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

void dvb_reader_free_eit_tables(GList *eit_tables)
{
    FLOG("\n");
    GList *tmp;
    for (tmp = eit_tables; tmp; tmp = g_list_next(tmp)) {
        g_list_free_full(((struct EITable *)tmp->data)->events, (GDestroyNotify)epg_event_free);
    }

    g_list_free_full(eit_tables, g_free);
}

void dvb_reader_reset(DVBReader *reader)
{
    FLOG("\n");
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
            dvb_reader_listener_clear_queue(listener);
            listener->have_pat = 0;
            listener->have_pmt = 0;
        }
    }

    dvb_reader_free_eit_tables(reader->eit_tables);
    reader->eit_tables = NULL;
    g_list_free_full(reader->active_pids, g_free);
    reader->active_pids = NULL;

    dvb_tuner_clean(reader->tuner);
}

void dvb_reader_destroy(DVBReader *reader)
{
    FLOG("\n");
    if (!reader)
        return;

    dvb_reader_stop(reader);

    DVBRecorderEvent *quit_event = dvb_recorder_event_new(DVB_RECORDER_EVENT_STOP_THREAD, NULL, NULL);
    dvb_reader_push_event_next(reader, quit_event);

    if (reader->event_thread) {
        g_thread_join(reader->event_thread);
        reader->event_thread = NULL;
    }


    g_queue_foreach(&reader->event_queue, (GFunc)dvb_recorder_event_destroy, NULL);
    g_queue_clear(&reader->event_queue);

    g_list_free_full(reader->listeners, (GDestroyNotify)dvb_reader_listener_free);
    g_list_free_full(reader->active_pids, g_free);
}

DVBStreamStatus dvb_reader_get_stream_status(DVBReader *reader)
{
    FLOG("\n");
    if (reader == NULL)
        return DVB_STREAM_STATUS_UNKNOWN;
    return reader->status;
}

static gint _dvb_reader_find_pid(struct DVBPidDescription *desc, uint16_t pid)
{
    if (desc->pid == pid)
        return 0;
    return 1;
}

void dvb_reader_add_active_pid(DVBReader *reader, uint16_t pid, DVBFilterType type)
{
    FLOG("\n");
    LOG(reader->logger, "Add active pid: %u, type 0x%04x\n", pid, type);

    struct DVBPidDescription *desc = NULL;
    GList *link;
    if ((link = g_list_find_custom(reader->active_pids,
                                   GUINT_TO_POINTER(pid),
                                   (GCompareFunc)_dvb_reader_find_pid)) != NULL
            && link->data != NULL) {
        desc = (struct DVBPidDescription *)link->data;
        LOG(reader->logger, "Double PID added, adding type: %u (type 0x%04x | 0x%04x)\n", pid, desc->type, type);
        desc->type |= type;
        return;
    }
    desc = g_malloc0(sizeof(struct DVBPidDescription));

    desc->pid = pid;
    desc->type = type;

    g_mutex_lock(&reader->tuner_mutex);
    dvb_tuner_add_pid(reader->tuner, pid);
    g_mutex_unlock(&reader->tuner_mutex);

    reader->active_pids = g_list_prepend(reader->active_pids, desc);
}

DVBFilterType dvb_reader_get_active_pid_type(DVBReader *reader, uint16_t pid)
{
    GList *tmp;
    for (tmp = reader->active_pids; tmp; tmp = g_list_next(tmp)) {
        if (((struct DVBPidDescription *)tmp->data)->pid == pid)
            return ((struct DVBPidDescription *)tmp->data)->type;
    }

    return DVB_FILTER_OTHER;
}

static gint dvb_reader_compare_listener_fd(struct DVBReaderListener *listener, gpointer fd)
{
    FLOG("\n");
    if (listener == NULL)
        return -1;
    if (listener->fd == GPOINTER_TO_INT(fd))
        return 0;
    return 1;
}

static gint dvb_reader_compare_listener_cb(struct DVBReaderListener *listener, gpointer callback)
{
    FLOG("\n");
    if (listener == NULL)
        return -1;
    if (listener->callback == callback)
        return 0;
    return 1;
}

void dvb_reader_set_listener(DVBReader *reader, DVBFilterType filter, int fd,
                             DVBReaderListenerCallback callback, gpointer userdata)
{
    FLOG("\n");
    fprintf(stderr, "dvb_reader_set_listener\n");
    g_return_if_fail(reader != NULL);

    LOG(reader->logger, "set listener %d (%p), mutex: %p\n", fd, callback, &reader->listener_mutex);

    g_mutex_lock(&reader->listener_mutex);

    GList *element = NULL;
    if (fd >= 0)
        element = g_list_find_custom(reader->listeners, GINT_TO_POINTER(fd), (GCompareFunc)dvb_reader_compare_listener_fd);
    else
        element = g_list_find_custom(reader->listeners, callback, (GCompareFunc)dvb_reader_compare_listener_cb);

    struct DVBReaderListener *listener = NULL;

    if (element) {
        listener = (struct DVBReaderListener *)element->data;
        g_mutex_lock(&listener->message_lock);
        g_queue_foreach(&listener->message_queue, (GFunc)g_free, NULL);
        g_queue_clear(&listener->message_queue);
        listener->terminate = 0;
        listener->write_error = 0;
        listener->eos = 0;
        listener->have_pat = 0;
        listener->have_pmt = 0;
        listener->running = 0;
        g_mutex_unlock(&listener->message_lock);
        listener->filter = filter;
        listener->userdata = userdata;
    }
    else {
        listener = g_malloc0(sizeof(struct DVBReaderListener));

        listener->fd = fd;
        listener->callback = callback;
        listener->userdata = userdata;
        listener->filter = filter;
        listener->reader = reader;
        g_queue_init(&listener->message_queue);
        g_mutex_init(&listener->message_lock);
        g_cond_init(&listener->message_cond);

        reader->listeners = g_list_prepend(reader->listeners, listener);

        listener->worker_thread = g_thread_new("Listener", (GThreadFunc)dvb_reader_listener_thread_proc, listener);
    }

    dvb_reader_listener_send_pat(reader, listener);
    dvb_reader_listener_send_pmt(reader, listener);

    g_mutex_unlock(&reader->listener_mutex);
}

void dvb_reader_listener_set_running(DVBReader *reader, int fd, DVBReaderListenerCallback callback, gboolean do_run)
{
    g_return_if_fail(reader != NULL);

    GList *element = NULL;
    if (fd >= 0)
        element = g_list_find_custom(reader->listeners, GINT_TO_POINTER(fd), (GCompareFunc)dvb_reader_compare_listener_fd);
    else
        element = g_list_find_custom(reader->listeners, callback, (GCompareFunc)dvb_reader_compare_listener_cb);

    if (!element)
        return;

    if (do_run)
        dvb_reader_listener_send_message((struct DVBReaderListener *)element->data, DVB_READER_LISTENER_MESSAGE_CONTINUE,
                                         NULL, 0, TRUE);
    else {
        g_mutex_lock(&((struct DVBReaderListener *)element->data)->message_lock);
        ((struct DVBReaderListener *)element->data)->running = 0;
        g_mutex_unlock(&((struct DVBReaderListener *)element->data)->message_lock);
    }
}

void dvb_reader_listener_clear_queue(struct DVBReaderListener *listener)
{
    if (!listener)
        return;
    g_mutex_lock(&listener->message_lock);
    g_queue_foreach(&listener->message_queue, (GFunc)g_free, NULL);
    g_queue_clear(&listener->message_queue);
    g_mutex_unlock(&listener->message_lock);
}

void dvb_reader_listener_free(struct DVBReaderListener *listener)
{
    FLOG(" listener: %p\n", listener);
    if (!listener)
        return;
    if (listener->worker_thread && !listener->terminate) {
        dvb_reader_listener_send_message(listener, DVB_READER_LISTENER_MESSAGE_QUIT, NULL, 0, TRUE);
        g_thread_join(listener->worker_thread);
        listener->worker_thread = NULL;
    }
    dvb_reader_listener_clear_queue(listener);
    g_free(listener);
}

void dvb_reader_remove_listener(DVBReader *reader, int fd, DVBReaderListenerCallback callback)
{
    FLOG("\n");
    g_return_if_fail(reader != NULL);

    LOG(reader->logger, "remove listener %d (%p), mutex: %p\n", fd, callback, &reader->listener_mutex);

    g_mutex_lock(&reader->listener_mutex);

    GList *element = NULL;
    if (fd < 0)
        element = g_list_find_custom(reader->listeners, callback, (GCompareFunc)dvb_reader_compare_listener_cb);
    else
        element = g_list_find_custom(reader->listeners, GINT_TO_POINTER(fd), (GCompareFunc)dvb_reader_compare_listener_fd);

    LOG(reader->logger, "found reader: %p\n", element ? element->data : NULL);

    if (element) {
        reader->listeners = g_list_remove_link(reader->listeners, element);
        g_mutex_unlock(&reader->listener_mutex);
    }
    else {
        g_mutex_unlock(&reader->listener_mutex);
    }

    if (element) {
        struct DVBReaderListener *listener = (struct DVBReaderListener *)element->data;
/*        if (listener->worker_thread) {
            dvb_reader_listener_send_message(listener, DVB_READER_LISTENER_MESSAGE_QUIT, NULL, 0, TRUE);
            fprintf(stderr, "join worker thread for %d %p\n", listener->fd, listener->callback);
            g_thread_join(listener->worker_thread);
            fprintf(stderr, "joined worker thread for %d %p\n", listener->fd, listener->callback);
        }*/
        dvb_reader_listener_free(listener);

        g_list_free_1(element);
    }

}

void dvb_reader_tune(DVBReader *reader,
                     guint32 frequency,
                     guint8  polarization,
                     guint8  sat_no,
                     guint32 symbol_rate,
                     guint8 delivery_system,
                     guint16 modulation,
                     guint16 roll_off,
                     guint16 program_number)
{
    FLOG("\n");
    g_return_if_fail(reader != NULL);

    /* FIXME: stop running stream first */
    LOG(reader->logger, "dvb_reader_tune: frequency: %" PRIu32 ", polarization: %d\n", frequency, polarization);

    DVBRecorderEvent *event = dvb_recorder_event_new(DVB_RECORDER_EVENT_TUNE_IN,
                                                     "frequency", frequency,
                                                     "polarization", polarization,
                                                     "sat_no", sat_no,
                                                     "symbol_rate", symbol_rate,
                                                     "program_number", program_number,
                                                     "delivery_system", delivery_system,
                                                     "modulation", modulation,
                                                     "roll_off", roll_off,
                                                     NULL, NULL);
    dvb_reader_push_event(reader, event);
}

void dvb_reader_start(DVBReader *reader)
{
    FLOG("\n");
    g_return_if_fail(reader != NULL);

    if (pipe(reader->control_pipe_stream) != 0)
        LOG(reader->logger, "Error creating control pipe.\n");

    reader->status = DVB_STREAM_STATUS_RUNNING;

    reader->data_thread = g_thread_new("DataThread", (GThreadFunc)dvb_reader_data_thread_proc, reader);
}

void dvb_reader_stop(DVBReader *reader)
{
    FLOG("\n");
    g_return_if_fail(reader != NULL);

    reader->status = DVB_STREAM_STATUS_STOPPED;
    LOG(reader->logger, "dvb_reader_stop: pipe_stream: %d/%d\n", reader->control_pipe_stream[0],
        reader->control_pipe_stream[1]);

    if (reader->control_pipe_stream[1] >= 0) {
        write(reader->control_pipe_stream[1], "quit", 4);
    }

    if (reader->data_thread) {
        /* FIXME: write quit here; lock data_thread before setting to NULL in proc */
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

GList *dvb_reader_get_events(DVBReader *reader)
{
    FLOG("\n");
    g_return_val_if_fail(reader != NULL, NULL);

    GList *complete = NULL;
    GList *tmp_table, *tmp;

    for (tmp_table = reader->eit_tables; tmp_table; tmp_table = g_list_next(tmp_table)) {
        for (tmp = ((struct EITable *)tmp_table->data)->events; tmp; tmp = g_list_next(tmp)) {
            complete = g_list_prepend(complete, tmp->data);
        }
    }

    return g_list_sort(complete, (GCompareFunc)epg_event_compare_time);
}

EPGEvent *dvb_reader_get_event(DVBReader *reader, guint16 eventid)
{
    FLOG("\n");
    GList *tmp_table, *tmp;
    for (tmp_table = reader->eit_tables; tmp_table; tmp_table = g_list_next(tmp_table)) {
        for (tmp = ((struct EITable *)tmp_table->data)->events; tmp; tmp = g_list_next(tmp)) {
            if (((EPGEvent *)tmp->data)->event_id == eventid)
                return (EPGEvent *)tmp->data;
        }
    }

    return NULL;
}

void dvb_reader_push_event(DVBReader *reader, DVBRecorderEvent *event)
{
    FLOG("\n");
    g_mutex_lock(&reader->event_mutex);

    g_queue_push_tail(&reader->event_queue, event);
    g_cond_signal(&reader->event_cond);

    g_mutex_unlock(&reader->event_mutex);
}

void dvb_reader_push_event_next(DVBReader *reader, DVBRecorderEvent *event)
{
    FLOG("\n");
    g_mutex_lock(&reader->event_mutex);

    g_queue_push_head(&reader->event_queue, event);
    g_cond_signal(&reader->event_cond);

    g_mutex_unlock(&reader->event_mutex);
}

DVBRecorderEvent *dvb_reader_pop_event(DVBReader *reader)
{
    FLOG("\n");
    DVBRecorderEvent *event;

    g_mutex_lock(&reader->event_mutex);

    while ((event = (DVBRecorderEvent *)g_queue_pop_head(&reader->event_queue)) == NULL)
        g_cond_wait(&reader->event_cond, &reader->event_mutex);

    g_mutex_unlock(&reader->event_mutex);

    return event;
}


void dvb_reader_event_handle_tune_in(DVBReader *reader, DVBRecorderEventTuneIn *event)
{
    FLOG("\n");
    LOG(reader->logger, "Tune In Handler\n");
    dvb_reader_stop(reader);
    int rc;
    g_mutex_lock(&reader->tuner_mutex);
    /* FIXME: make this cancellable */
    LOG(reader->logger, "dvb_reader_event_handle_tune_in frequency: %" PRIu32 ", pol: %d, srate: %d\n", event->frequency, event->polarization, event->symbol_rate);
    DVBTunerConfiguration tuner_config = {
        .frequency = event->frequency,
        .polarization = event->polarization,
        .sat_no = event->sat_no,
        .symbolrate = event->symbol_rate,
        .delivery_system = event->delivery_system,
        .modulation = event->modulation,
        .roll_off = event->roll_off
    };
    rc = dvb_tuner_tune(reader->tuner, &tuner_config, NULL, 0);
    if (rc == 0) {
        reader->frequency = event->frequency;
        reader->polarization = event->polarization;
        reader->sat_no = event->sat_no;
        reader->symbol_rate = event->symbol_rate;
        reader->program_number = event->program_number;
    }

    reader->tuner_fd = dvb_tuner_get_fd(reader->tuner);
    g_mutex_unlock(&reader->tuner_mutex);

    LOG(reader->logger, "[lib]: rc: %d, tuner fd: %d\n", rc, reader->tuner_fd);

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
    FLOG("\n");
    DVBRecorderEvent *event;

    while (1) {
        event = dvb_reader_pop_event(reader);

        LOG(reader->logger, "reader_event_thread_proc event: %d\n", event->type);

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
    FLOG("\n");
    LOG(reader->logger, "dvb_reader_data_thread_proc\n");
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


    uint8_t buffer[8*4096];
    ssize_t bytes_read;
    struct pollfd pfd[2];

    pfd[0].fd = reader->control_pipe_stream[0];
    pfd[0].events = POLLIN;

    LOG(reader->logger, "tuner fd: %d\n", reader->tuner_fd);
    if (reader->tuner_fd < 0) {
        /* send event tuning failed */
        goto done;
    }
    pfd[1].fd = reader->tuner_fd;
    pfd[1].events = POLLIN;

    DVBStreamStatus exit_status = DVB_STREAM_STATUS_UNKNOWN;

    while (1) {
        if (poll(pfd, 2, 15000)) {
            if (pfd[1].revents & POLLIN) {
                bytes_read = read(pfd[1].fd, buffer, 8*4096);
                if (bytes_read <= 0) {
                    if (bytes_read == 0) {
                        LOG(reader->logger, "reached EOF\n");
                        exit_status = DVB_STREAM_STATUS_EOS;
                        break;
                    }
                    if (errno == EAGAIN)
                        continue;
                    if (errno == EOVERFLOW) {
                        LOG(reader->logger, "Overflow, continue\n");
                        continue;
                    }
                    LOG(reader->logger, "Error reading data. Stopping thread. (%d) %s\n", errno, strerror(errno));
                    exit_status = DVB_STREAM_STATUS_EOS;
                    break;
                }
                ts_reader_push_buffer(ts_reader, buffer, bytes_read);
            }
            if (pfd[0].revents & POLLIN || pfd[0].revents & POLLNVAL) {
                LOG(reader->logger, "Received data on control pipe. Stop thread.\n");
                exit_status = DVB_STREAM_STATUS_STOPPED;
                break;
            }
            if (pfd[1].revents & POLLNVAL || pfd[1].revents & POLLHUP || pfd[1].revents & POLLERR) {
                LOG(reader->logger, "Input closed\n");
                exit_status = DVB_STREAM_STATUS_EOS;
                break;
            }
        }
#ifdef DVB_TUNER_DUMMY
        usleep(10000);
#endif
    }

done:
    ts_reader_free(ts_reader);

    reader->data_thread = NULL;

    LOG(reader->logger, "Stream stopped\n");
    dvb_reader_listener_broadcast_message(reader, DVB_READER_LISTENER_MESSAGE_EOS, NULL, 0, FALSE);

    dvb_recorder_event_send(DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED,
            reader->event_cb, reader->event_data,
            "status", exit_status,
            NULL, NULL);

    return NULL;
}

gchar *dvb_reader_get_running_program(DVBReader *reader)
{
    FLOG("\n");
    g_return_val_if_fail(reader != NULL, NULL);

    GList *tmp;
    struct EITable *table = NULL;
    EPGEvent *event = NULL;
    EPGEvent *match = NULL;

    for (tmp = reader->eit_tables; tmp; tmp = g_list_next(tmp)) {
        if (((struct EITable *)tmp->data)->table_id == 0x4e) { /* current/next program */
            table = (struct EITable *)tmp->data;
            break;
        }
    }

    if (!table)
        return NULL;

    time_t current_time = time(NULL);

    /* FIXME: if event starts in a few (configurable) seconds, use next program */

    for (tmp = table->events; tmp; tmp = g_list_next(tmp)) {
        event = (EPGEvent *)tmp->data;
        LOG(reader->logger, "get_running_program: starttime: %lu, running_status: %d\n",
                event->starttime, event->running_status);
        if (event->running_status == EPGEventStatusRunning) {
            match = event;
            break;
        }
        if (!match && event->starttime <= current_time && current_time <= event->starttime + event->duration)
            match = event;
    }

    if (match && match->short_descriptions)
        return g_strdup(((EPGShortEvent *)match->short_descriptions->data)->description);
    else
        return NULL;
}

DVBStreamInfo *dvb_reader_get_stream_info(DVBReader *reader)
{
    FLOG("\n");
    g_return_val_if_fail(reader != NULL, NULL);

    DVBStreamInfo *info = g_malloc0(sizeof(DVBStreamInfo));

    info->service_provider = reader->service_info ? g_strdup(reader->service_info->provider) : NULL;
    info->service_name = reader->service_info ? g_strdup(reader->service_info->name) : NULL;
    info->service_type = reader->service_info ? reader->service_info->type : 0;

    LOG(reader->logger, "Service info: provider=%s, name=%s, type=%u\n",
            info->service_provider,
            info->service_name,
            info->service_type);

    /* FIXME: read this from current eit (0x48) */
    info->program_title = dvb_reader_get_running_program(reader);

    return info;
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
    LOG(reader->logger, "pat_cb: have_pat: %d\n", reader->dvbpsi_have_pat);
    if (reader->dvbpsi_have_pat) {
        dvbpsi_pat_delete(pat);
        return;
    }

    LOG(reader->logger, "pat_cb: current_next=%u, ts_id=%u, version=%u\n", pat->b_current_next, pat->i_ts_id, pat->i_version);

    dvbpsi_pat_program_t *prog;

    for (prog = pat->p_first_program; prog; prog = prog->p_next) {
        LOG(reader->logger, "pat_cb: pat prog number=%u, pid=%u, want prog %u\n",
                prog->i_number, prog->i_pid, reader->program_number);
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

    LOG(reader->logger, "pat_cb: pat packet count: %u\n", reader->pat_packet_count);
    if (reader->pat_packet_count) {
        reader->dvbpsi_have_pat = 1;

        GList *tmp;
        g_mutex_lock(&reader->listener_mutex);
        for (tmp = reader->listeners; tmp; tmp = g_list_next(tmp)) {
            dvb_reader_listener_send_pat(reader, (struct DVBReaderListener *)tmp->data);
        }
        g_mutex_unlock(&reader->listener_mutex);
    }
}

void dvb_reader_dvbpsi_pmt_cb(DVBReader *reader, dvbpsi_pmt_t *pmt)
{
    LOG(reader->logger, "pmt_cb: have_pmt: %d\n", reader->dvbpsi_have_pmt);
    if (reader->dvbpsi_have_pmt) {
        dvbpsi_pmt_delete(pmt);
        return;
    }

    dvb_reader_rewrite_pmt(reader, pmt);

    dvbpsi_pmt_es_t *stream;
    DVBFilterType type;
    for (stream = pmt->p_first_es; stream; stream = stream->p_next) {
        /* iso13818 table 2-29 */
        switch (stream->i_type) {
            case 0x01:
            case 0x02:
            case 0x1b:
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
                type = DVB_FILTER_OTHER;
                break;
        }

        dvb_reader_add_active_pid(reader, stream->i_pid, type);
    }

    if (pmt->i_pcr_pid != 0x1ff)
        dvb_reader_add_active_pid(reader, pmt->i_pcr_pid, DVB_FILTER_PCR);

    dvbpsi_pmt_delete(pmt);

    reader->dvbpsi_have_pmt = 1;

    GList *tmp;
    g_mutex_lock(&reader->listener_mutex);
    for (tmp = reader->listeners; tmp; tmp = g_list_next(tmp)) {
        dvb_reader_listener_send_pmt(reader, (struct DVBReaderListener *)tmp->data);
    }
    g_mutex_unlock(&reader->listener_mutex);
}

void dvb_reader_dvbpsi_eit_cb(DVBReader *reader, dvbpsi_eit_t *eit)
{
    LOG(reader->logger, "eit_cb\n");
    /* get table id, find in list, or create new, insert */
    struct EITable *table = NULL;
    GList *table_entry = g_list_find_custom(reader->eit_tables, GUINT_TO_POINTER(eit->i_table_id),
                                            (GCompareFunc)dvb_reader_find_table_id);

    if (table_entry)
        table = (struct EITable *)table_entry->data;

    /* only update if table has not been read or if table has a new version */
    if (table && table->version == eit->i_table_id)
        goto done;

    if (table == NULL) {
        LOG(reader->logger, "eit add table: 0x%02x\n", eit->i_table_id);
        table = g_malloc0(sizeof(struct EITable));
        table->table_id = eit->i_table_id;
        table->version = eit->i_version;
        reader->eit_tables = g_list_insert_sorted(reader->eit_tables, table,
                                                  (GCompareFunc)dvb_reader_compare_event_tables_id);
    }

    g_list_free_full(table->events, (GDestroyNotify)epg_event_free);

    table->events = epg_read_table(eit);

    dvb_recorder_event_send(DVB_RECORDER_EVENT_EIT_CHANGED,
            reader->event_cb, reader->event_data,
            "table-id", GUINT_TO_POINTER(eit->i_table_id),
            NULL, NULL);

done:
    dvbpsi_eit_delete(eit);
}

void dvb_reader_dvbpsi_sdt_cb(DVBReader *reader, dvbpsi_sdt_t *sdt)
{
    if (reader->dvbpsi_have_sdt) {
        dvbpsi_sdt_delete(sdt);
        return;
    }

    LOG(reader->logger, "sdt_cb: cur/next=%u, version=%u, ext=%u, networkid=%u, tableid=%u\n",
            sdt->b_current_next, sdt->i_version, sdt->i_extension, sdt->i_network_id, sdt->i_table_id);

    dvbpsi_sdt_service_t *service;
    GList *desc_list = NULL;
    for (service = sdt->p_first_service; service; service = service->p_next) {
        LOG(reader->logger, "sdt_cb, std service_id=%u, want %u\n",
                service->i_service_id, reader->program_number);
        if (service->i_service_id == reader->program_number) {
            desc_list = dvb_reader_dvbpsi_handle_descriptors(reader, service->p_first_descriptor);
            break;
        }
    }

    GList *tmp;
    for (tmp = desc_list; tmp; tmp = g_list_next(tmp)) {
        LOG(reader->logger, "sdt_cb, tag=0x%02x, want 0x%02x\n",
                ((dvb_si_descriptor *)tmp->data)->tag,
                dvb_si_tag_service_descriptor);
        if (((dvb_si_descriptor *)tmp->data)->tag == dvb_si_tag_service_descriptor) {
            reader->service_info = (dvb_si_descriptor_service *)tmp->data;
            tmp->data = NULL;
            break;
        }
    }

    LOG(reader->logger, "SDT: service_info: %p\n", reader->service_info);
    if (reader->service_info) {
        reader->dvbpsi_have_sdt = 1;

        LOG(reader->logger, "send DVB_RECORDER_EVENT_SDT_CHANGED\n");
        dvb_recorder_event_send(DVB_RECORDER_EVENT_SDT_CHANGED,
                reader->event_cb, reader->event_data,
                NULL, NULL);
    }

    dvbpsi_sdt_delete(sdt);
    g_list_free_full(desc_list, (GDestroyNotify)dvb_si_descriptor_free);
}

void dvb_reader_dvbpsi_rst_cb(DVBReader *reader, dvbpsi_rst_t *rst)
{
    LOG(reader->logger, "rst_cb\n");

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
    if (packets == NULL) {
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
    else
        g_free(buffer);
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
    LOG(reader->logger, "rewrite pat to %u packets\n", reader->pat_packet_count);

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

void dvb_reader_listener_send_message(struct DVBReaderListener *listener, enum DVBReaderListenerMessageType type,
                                      uint8_t *data, gsize size, gboolean immediately)
{
    struct DVBReaderListenerMessage *msg = g_malloc(sizeof(struct DVBReaderListenerMessage));

    msg->type = type;
    msg->data_size = size;
    if (size > 0)
        memcpy(msg->data, data, size);

    g_mutex_lock(&listener->message_lock);

    if (immediately)
        g_queue_push_head(&listener->message_queue, msg);
    else
        g_queue_push_tail(&listener->message_queue, msg);

    g_cond_signal(&listener->message_cond);

    g_mutex_unlock(&listener->message_lock);
}

void dvb_reader_listener_broadcast_message(DVBReader *reader, enum DVBReaderListenerMessageType type,
                                           uint8_t *data, gsize size, gboolean immediately)
{
    FLOG("\n");
    GList *tmp;

    g_mutex_lock(&reader->listener_mutex);
    for (tmp = reader->listeners; tmp; tmp = g_list_next(tmp)) {
        dvb_reader_listener_send_message((struct DVBReaderListener *)tmp->data, type, data, size, immediately);
    }
    g_mutex_unlock(&reader->listener_mutex);
}

struct DVBReaderListenerMessage *dvb_reader_listener_pop_message(struct DVBReaderListener *listener)
{
    FLOG("\n");
    struct DVBReaderListenerMessage *msg;

    g_mutex_lock(&listener->message_lock);

    while ((msg = (struct DVBReaderListenerMessage *)g_queue_peek_head(&listener->message_queue)) == NULL ||
            (listener->running == 0 && msg->type != DVB_READER_LISTENER_MESSAGE_CONTINUE &&
                                       msg->type != DVB_READER_LISTENER_MESSAGE_QUIT))
        g_cond_wait(&listener->message_cond, &listener->message_lock);

    msg = g_queue_pop_head(&listener->message_queue);

    g_mutex_unlock(&listener->message_lock);

    return msg;
}

void dvb_reader_listener_drop_data_messages(struct DVBReaderListener *listener)
{
    FLOG("\n");
    GList *tmp, *next;

    g_mutex_lock(&listener->message_lock);

    tmp = listener->message_queue.head;
    while (tmp) {
        next = tmp->next;
        if (((struct DVBReaderListenerMessage *)tmp->data)->type == DVB_READER_LISTENER_MESSAGE_DATA) {
            g_free(tmp->data);
            g_queue_delete_link(&listener->message_queue, tmp);
        }
        tmp = next;
    }

    g_mutex_unlock(&listener->message_lock);
}

/* Write packet to internal listener buffer if filter matches. When the buffer is full create a new DATA message,
 * send it, and clear the buffer. */
void dvb_reader_listener_push_packet(struct DVBReaderListener *listener, DVBFilterType type, const uint8_t *packet)
{
    if ((listener->filter & (DVB_FILTER_ALL & ~(DVB_FILTER_PAT | DVB_FILTER_PMT))) & type) {
        memcpy(&listener->buffer[listener->buffer_size], packet, TS_SIZE);
        listener->buffer_size += TS_SIZE;
    }

    if (listener->buffer_size >= (DVB_LISTENER_BUFFER_SIZE / TS_SIZE) * TS_SIZE) {
        dvb_reader_listener_send_message(listener, DVB_READER_LISTENER_MESSAGE_DATA,
                                         listener->buffer, listener->buffer_size, FALSE);
        listener->buffer_size = 0;
    }
}

gint dvb_reader_listener_write_data_full(struct DVBReaderListener *listener, const uint8_t *data, gsize size)
{
    FLOG("\n");
    ssize_t nw, offset;
    unsigned long int error_enc = 0;
    struct pollfd pfd[1];
    int rc;
    pfd[0].fd = listener->fd;
    pfd[0].events = POLLOUT;
    for (offset = 0; offset < size; offset += nw) {
        nw = 0;
        if ((rc = poll(pfd, 1, 1000)) <= 0) {
            if (rc == 0) {
                LOG(listener->reader->logger, "Writing to %d timed out, count %lu.\n", listener->fd, error_enc);
                if (++error_enc > 10 || listener->terminate) {
                    return -1;
                }

                continue;
            }
            if (rc == -1) {
                LOG(listener->reader->logger, "Poll returned an error: %d %s\n", errno, strerror(errno));
                return -1;
            }
        }
        if ((nw = write(listener->fd, data + offset, (size_t)(size - offset))) <= 0) {
            if (nw < 0) {
                if (errno == EAGAIN) {
                    LOG(listener->reader->logger,
                        "EAGAIN on fd %d while trying to write %zd bytes.\n",
                        listener->fd,
                        (size_t)(size - offset));
                    nw = 0;
                    ++error_enc;
                    continue;
                }
                LOG(listener->reader->logger, "Could not write to %d.\n", listener->fd);
                return -1;
            }
            else if (nw == 0) {
                LOG(listener->reader->logger, "Written zero bytes to %d.\n", listener->fd);
            }
            break;
        }
    }

    if (error_enc)
        LOG(listener->reader->logger, "written successfully after encountering %lu errors.\n", error_enc);

    return 1;
}

gpointer dvb_reader_listener_thread_proc(struct DVBReaderListener *listener)
{
    FLOG("\n");
    struct DVBReaderListenerMessage *msg;
    gint rc;

    LOG(listener->reader->logger, "dvb_reader_listener_thread_proc for %d, %p\n", listener->fd, listener->callback);

    while (1) {
        msg = dvb_reader_listener_pop_message(listener);

        switch (msg->type) {
            case DVB_READER_LISTENER_MESSAGE_DATA:
                if (listener->fd >= 0 && !listener->write_error) {
                    if ((rc = dvb_reader_listener_write_data_full(listener, msg->data, msg->data_size)) <= 0) {
                        if (rc < 0) {
                            listener->write_error = 1;
                            LOG(listener->reader->logger, "signal write error\n");
                            dvb_recorder_event_send(DVB_RECORDER_EVENT_LISTENER_STATUS_CHANGED,
                                    listener->reader->event_cb, listener->reader->event_data,
                                    "status", DVB_LISTENER_STATUS_WRITE_ERROR,
                                    "fd", listener->fd,
                                    "cb", listener->callback,
                                    NULL, NULL);
                        }
                    }
                }
                if (listener->callback) {
                    listener->callback(msg->data, msg->data_size, listener->userdata);
                }
                break;
            case DVB_READER_LISTENER_MESSAGE_DROP:
                LOG(listener->reader->logger, "listener got DROP message\n");
                dvb_reader_listener_drop_data_messages(listener);
                break;
            case DVB_READER_LISTENER_MESSAGE_CONTINUE:
                LOG(listener->reader->logger, "listener got CONTINUE message\n");
                g_mutex_lock(&listener->message_lock);
                listener->running = 1;
                g_mutex_unlock(&listener->message_lock);
                break;
            case DVB_READER_LISTENER_MESSAGE_QUIT:
                LOG(listener->reader->logger, "listener got QUIT message\n");
                g_free(msg);
                listener->terminate = 1;
                if (listener->reader)
                    dvb_recorder_event_send(DVB_RECORDER_EVENT_LISTENER_STATUS_CHANGED,
                            listener->reader->event_cb, listener->reader->event_data,
                            "status", DVB_LISTENER_STATUS_TERMINATED,
                            "fd", listener->fd,
                            "cb", listener->callback,
                            NULL, NULL);
                return NULL;
            case DVB_READER_LISTENER_MESSAGE_EOS:
                LOG(listener->reader->logger, "listener got EOS message\n");
                listener->eos = 1;
                if (listener->reader)
                    dvb_recorder_event_send(DVB_RECORDER_EVENT_LISTENER_STATUS_CHANGED,
                            listener->reader->event_cb, listener->reader->event_data,
                            "status", DVB_LISTENER_STATUS_EOS,
                            "fd", listener->fd,
                            "cb", listener->callback,
                            NULL, NULL);
                break;
        }

        g_free(msg);
    }

    LOG(listener->reader->logger, "listener: %d %p reached the unreachable\n", listener->fd, listener->callback);
    return NULL;
}

void _dump_packet(DVBRecorderLogger *logger, const uint8_t *packet)
{
    uint16_t i;
    gchar buf[64];
    for (i = 0; i < TS_SIZE; ++i) {
        sprintf(&buf[(i % 16) * 3], "%02x ", packet[i]);
        if (i % 16 == 15 || i+1 == TS_SIZE)
            LOG(logger, "%s\n", buf);
    }
}

void dvb_reader_listener_send_pat(DVBReader *reader, struct DVBReaderListener *listener)
{
    FLOG("\n");
    LOG(reader->logger, "Send PAT to listener (%u)\n", reader->pat_packet_count);
    if (reader->pat_packet_count == 0)
        return;
    if (listener->have_pat)
        return;

    uint8_t i;
    gsize remaining = reader->pat_packet_count * TS_SIZE;
    gsize buf_size;
    gsize offset = 0;
    while (remaining) {
        if (remaining < DVB_LISTENER_BUFFER_SIZE)
            buf_size = remaining;
        else
            buf_size = DVB_LISTENER_BUFFER_SIZE;
        dvb_reader_listener_send_message(listener, DVB_READER_LISTENER_MESSAGE_DATA, &reader->pat_data[offset], buf_size, FALSE);
        remaining -= buf_size;
        offset += buf_size;
    }

    for (i = 0; i < reader->pat_packet_count; ++i) {
        LOG(reader->logger, "PAT[%d]:\n", i);
        _dump_packet(reader->logger, &reader->pat_data[i * TS_SIZE]);
/*        dvb_reader_listener_push_packet(listener, DVB_FILTER_PAT, &reader->pat_data[i * TS_SIZE]);*/

    }
    listener->have_pat = 1;
}

void dvb_reader_listener_send_pmt(DVBReader *reader, struct DVBReaderListener *listener)
{
    FLOG("\n");
    LOG(reader->logger, "Send PMT to listener\n");
    if (reader->pmt_packet_count == 0)
        return;

    if (!listener->have_pat || listener->have_pmt)
        return;

    uint8_t i;
    gsize remaining = reader->pmt_packet_count * TS_SIZE;
    gsize buf_size;
    gsize offset = 0;
    while (remaining) {
        if (remaining < DVB_LISTENER_BUFFER_SIZE)
            buf_size = remaining;
        else
            buf_size = DVB_LISTENER_BUFFER_SIZE;
        dvb_reader_listener_send_message(listener, DVB_READER_LISTENER_MESSAGE_DATA, &reader->pmt_data[offset], buf_size, FALSE);
        remaining -= buf_size;
        offset += buf_size;
    }

    for (i = 0; i < reader->pmt_packet_count; ++i) {
        LOG(reader->logger, "PMT[%d]:\n", i);
        _dump_packet(reader->logger, &reader->pmt_data[i * TS_SIZE]);
   /*     dvb_reader_listener_push_packet(listener, DVB_FILTER_PMT, &reader->pmt_data[i * TS_SIZE]); */
    }
    listener->have_pmt = 1;
}

gboolean dvb_reader_get_current_pat_packets(DVBReader *reader, guint8 **buffer, gsize *length)
{
    FLOG("\n");
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
    FLOG("\n");
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
    DVBFilterType type = dvb_reader_get_active_pid_type(reader, ts_get_pid(packet));

    g_mutex_lock(&reader->listener_mutex);
    for (tmp = reader->listeners; tmp; tmp = g_list_next(tmp)) {
        listener = (struct DVBReaderListener *)tmp->data;
        dvb_reader_listener_push_packet(listener, type, packet);
    }
    g_mutex_unlock(&reader->listener_mutex);
    return TRUE;
}

gboolean dvb_reader_handle_packet(const uint8_t *packet, void *userdata)
{
    DVBReader *reader = (DVBReader *)userdata;
    uint16_t pid = ts_get_pid(packet);

    /* special tables are on pids 0x0000 to 0x001f, we only handle pat (0x00), eit (0x12), sdt (0x11), rst (0x13),
     * and pmt (via pat), write all others directly and skip check */
    if (pid > 0x001f && pid != reader->dvbpsi_table_pids[TS_TABLE_PMT])
        goto done;

    uint8_t i;
    for (i = 0; i < N_TS_TABLE_TYPES; ++i) {
        if (reader->dvbpsi_table_pids[i] == pid) {
            if (reader->dvbpsi_handles[i])
                dvbpsi_packet_push(reader->dvbpsi_handles[i], (uint8_t *)packet);
            break;
        }
    }

done:
    return dvb_reader_write_packet(reader, packet);
}

float dvb_reader_query_signal_strength(DVBReader *reader)
{
    if (reader)
        return dvb_tuner_get_signal_strength(reader->tuner);
    return -1.0f;
}
