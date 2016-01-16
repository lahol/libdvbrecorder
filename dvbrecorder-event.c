#include "dvbrecorder-event.h"
#include <stdarg.h>
#include <stdio.h>

struct DREventClass {
    DVBRecorderEventType type;
    gsize size;
    void (*event_set_property)(DVBRecorderEvent *, const gchar *, const gpointer);
    void (*event_destroy)(DVBRecorderEvent *);
};

void dvb_recorder_event_tuned_set_property(DVBRecorderEvent *event,
                                           const gchar *prop_name, const gpointer prop_value);
void dvb_recorder_event_stream_status_changed_set_property(DVBRecorderEvent *event,
                                                           const gchar *prop_name, const gpointer prop_value);
void dvb_recorder_event_source_fd_changed_set_property(DVBRecorderEvent *event,
                                                       const gchar *prop_name, const gpointer prop_value);
void dvb_recorder_event_tune_in_set_property(DVBRecorderEvent *event,
                                             const gchar *prop_name, const gpointer prop_value);
void dvb_recorder_event_stop_thread_set_property(DVBRecorderEvent *event,
                                                 const gchar *prop_name, const gpointer prop_value);
void dvb_recorder_event_record_status_changed_set_property(DVBRecorderEvent *event,
        const gchar *prop_name, const gpointer prop_value);

static struct DREventClass event_classes[] = {
    { DVB_RECORDER_EVENT_TUNED, sizeof(DVBRecorderEventTuned),
        dvb_recorder_event_tuned_set_property, NULL },
    { DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED, sizeof(DVBRecorderEventStreamStatusChanged),
        dvb_recorder_event_stream_status_changed_set_property, NULL },
    { DVB_RECORDER_EVENT_SOURCE_FD_CHANGED, sizeof(DVBRecorderEventSourceFdChanged),
        dvb_recorder_event_source_fd_changed_set_property, NULL },
    { DVB_RECORDER_EVENT_TUNE_IN, sizeof(DVBRecorderEventTuneIn),
        dvb_recorder_event_tune_in_set_property, NULL },
    { DVB_RECORDER_EVENT_STOP_THREAD, sizeof(DVBRecorderEventStopThread),
        dvb_recorder_event_stop_thread_set_property, NULL },
    { DVB_RECORDER_EVENT_RECORD_STATUS_CHANGED, sizeof(DVBRecorderEventRecordStatusChanged),
        dvb_recorder_event_record_status_changed_set_property, NULL },
};

struct DREventClass *dvb_recorder_event_get_class(DVBRecorderEventType type)
{
    if (type >= DVB_RECORDER_EVENT_COUNT)
        return NULL;
    return &event_classes[type];
}

DVBRecorderEvent *dvb_recorder_event_new_valist(DVBRecorderEventType type, va_list ap)
{
    struct DREventClass *cls = dvb_recorder_event_get_class(type);
    if (!cls || cls->size == 0)
        return NULL;
    DVBRecorderEvent *event = g_malloc0(cls->size);
    event->type = type;


    gchar *prop_name;
    gpointer prop_value;

    while (1) {
        prop_name = va_arg(ap, gchar *);
        prop_value = va_arg(ap, gpointer);
        if (prop_name == NULL)
            break;
        if (cls->event_set_property)
            cls->event_set_property(event, prop_name, prop_value);
    }


    return event;
}

DVBRecorderEvent *dvb_recorder_event_new(DVBRecorderEventType type, ...)
{
    DVBRecorderEvent *event = NULL;
    va_list ap;
    va_start(ap, type);

    event = dvb_recorder_event_new_valist(type, ap);

    va_end(ap);

    return event;
}

void dvb_recorder_event_set_property(DVBRecorderEvent *event, const gchar *prop_name, const gpointer prop_value)
{
    if (!event)
        return;
    struct DREventClass *cls = dvb_recorder_event_get_class(event->type);
    if (!cls || !cls->event_set_property)
        return;
    cls->event_set_property(event, prop_name, prop_value);
}

void dvb_recorder_event_destroy(DVBRecorderEvent *event)
{
    if (!event)
        return;
    struct DREventClass *cls = dvb_recorder_event_get_class(event->type);
    if (cls && cls->event_destroy)
        cls->event_destroy(event);

    g_free(event);
}

void dvb_recorder_event_send(DVBRecorderEventType type, DVBRecorderEventCallback cb, gpointer data, ...)
{
    if (!cb)
        return;

    DVBRecorderEvent *event = NULL;
    va_list ap;
    va_start(ap, data);
    event = dvb_recorder_event_new_valist(type, ap);
    va_end(ap);

    if (!event)
        return;

    cb(event, data);

    dvb_recorder_event_destroy(event);
}

void dvb_recorder_event_tuned_set_property(DVBRecorderEvent *event,
                                         const gchar *prop_name, const gpointer prop_value)
{
    if (!event)
        return;
    DVBRecorderEventTuned *ev = (DVBRecorderEventTuned *)event;

    if (g_strcmp0(prop_name, "fd") == 0) {
        ev->fd = GPOINTER_TO_INT(prop_value);
    }
    else {
        fprintf(stderr, "Unknown property: %s\n", prop_name);
    }
}

void dvb_recorder_event_stream_status_changed_set_property(DVBRecorderEvent *event,
                                                         const gchar *prop_name, const gpointer prop_value)
{
    if (!event)
        return;
    DVBRecorderEventStreamStatusChanged *ev = (DVBRecorderEventStreamStatusChanged *)event;

    if (g_strcmp0(prop_name, "status") == 0) {
        ev->status = GPOINTER_TO_INT(prop_value);
    }
    else {
        fprintf(stderr, "Unknown property: %s\n", prop_name);
    }
}

void dvb_recorder_event_source_fd_changed_set_property(DVBRecorderEvent *event,
                                                       const gchar *prop_name, const gpointer prop_value)
{
    if (!event)
        return;
    DVBRecorderEventSourceFdChanged *ev = (DVBRecorderEventSourceFdChanged *)event;

    if (g_strcmp0(prop_name, "fd") == 0) {
        ev->fd = GPOINTER_TO_INT(prop_value);
    }
    else {
        fprintf(stderr, "Unknown property: %s\n", prop_name);
    }
}

void dvb_recorder_event_tune_in_set_property(DVBRecorderEvent *event,
                                             const gchar *prop_name, const gpointer prop_value)
{
    if (!event)
        return;
    DVBRecorderEventTuneIn *ev = (DVBRecorderEventTuneIn *)event;

    if (g_strcmp0(prop_name, "frequency") == 0) {
        ev->frequency = GPOINTER_TO_UINT(prop_value);
    }
    else if (g_strcmp0(prop_name, "polarization") == 0) {
        ev->frequency = GPOINTER_TO_UINT(prop_value);
    }
    else if (g_strcmp0(prop_name, "sat_no") == 0) {
        ev->sat_no = GPOINTER_TO_UINT(prop_value);
    }
    else if (g_strcmp0(prop_name, "symbol_rate") == 0) {
        ev->symbol_rate = GPOINTER_TO_UINT(prop_value);
    }
    else if (g_strcmp0(prop_name, "program_number") == 0) {
        ev->program_number = GPOINTER_TO_UINT(prop_value);
    }
    else {
        fprintf(stderr, "Unknown property: %s\n", prop_name);
    }
}

void dvb_recorder_event_stop_thread_set_property(DVBRecorderEvent *event,
                                                 const gchar *prop_name, const gpointer prop_value)
{
}

void dvb_recorder_event_record_status_changed_set_property(DVBRecorderEvent *event,
        const gchar *prop_name, const gpointer prop_value)
{
    if (!event)
        return;
    DVBRecorderEventRecordStatusChanged *ev = (DVBRecorderEventRecordStatusChanged *)event;

    if (g_strcmp0(prop_name, "status") == 0) {
        ev->status = GPOINTER_TO_INT(prop_value);
    }
    else {
        fprintf(stderr, "Unknown property: %s\n", prop_name);
    }
}
