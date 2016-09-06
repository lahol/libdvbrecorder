#include "timed-events.h"
#include "dvbrecorder.h"
#include "dvbrecorder-internal.h"
#include <stdio.h>

gint timed_event_compare_time(const TimedEvent *a, const TimedEvent *b)
{
    return (gint)(a->event_time - b->event_time);
}

void dvb_recorder_add_timed_event(DVBRecorder *recorder, TimedEvent *event)
{
    g_return_if_fail(event != NULL);

    GList *current_start = recorder->timed_events;

    recorder->timed_events = g_list_insert_sorted(recorder->timed_events, event, (GCompareFunc)timed_event_compare_time);

    if (current_start != recorder->timed_events) {
        /* if precise timer is running, kill it */
    }
}

void dvb_recorder_timed_event_run(DVBRecorder *recorder, TimedEvent *event)
{
    switch (event->type) {
        case TIMED_EVENT_TUNE_IN:
            fprintf(stderr, "timed event tune in\n");
            dvb_recorder_set_channel(recorder, ((TimedEventTuneIn *)event)->channel_id);
            break;
        case TIMED_EVENT_RECORD_START:
            fprintf(stderr, "timed event record start\n");
            dvb_recorder_record_start(recorder);
            break;
        case TIMED_EVENT_RECORD_STOP:
            fprintf(stderr, "timed event record stop\n");
            dvb_recorder_record_stop(recorder);
            break;
        default:
            fprintf(stderr, "Invalid timed event\n");
            break;
    }
}

struct _TimedEventSourceData {
    DVBRecorder *recorder;
    TimedEvent *event;
};

gboolean dvb_recorder_check_timed_event(struct _TimedEventSourceData *data)
{
    time_t current = time(NULL);
    if (data->event->event_time <= current) {
        data->recorder->scheduled_event_source = 0;
        fprintf(stderr, "check timed event: %" G_GUINT64_FORMAT " <= %" G_GUINT64_FORMAT "\n",
                data->event->event_time, current);
        dvb_recorder_timed_event_run(data->recorder, data->event);
        g_free(data->event);
        g_free(data);
        return FALSE;
    }
    return TRUE;
}

/* pop next event from queue and run idle func to check */
void dvb_recorder_schedule_timed_event(DVBRecorder *recorder)
{
    fprintf(stderr, "dvb_recorder_schedule_timed_event (event_source = %u)\n", recorder->scheduled_event_source);
    if (recorder->scheduled_event_source > 0)
        return;

    struct _TimedEventSourceData *data = g_malloc(sizeof(struct _TimedEventSourceData));
    data->recorder = recorder;
    data->event    = (TimedEvent *)recorder->timed_events->data;

    recorder->timed_events = g_list_remove_link(recorder->timed_events, recorder->timed_events);

    recorder->scheduled_event_source = g_idle_add((GSourceFunc)dvb_recorder_check_timed_event, data);
}

gboolean dvb_recorder_check_timed_events(DVBRecorder *recorder)
{
    fprintf(stderr, "dvb_recorder_check_timed_events\n");
    if (!recorder)
        return TRUE;
    if (!recorder->timed_events)
        return TRUE;
    if (((TimedEvent *)recorder->timed_events->data)->event_time <= time(NULL) + 30) {
        dvb_recorder_schedule_timed_event(recorder);
    }
    return TRUE;
}

void dvb_recorder_timed_events_clear(DVBRecorder *recorder)
{
    g_return_if_fail(recorder != NULL);
    g_list_free_full(recorder->timed_events, g_free);
    recorder->timed_events = NULL;

    if (recorder->scheduled_event_source) {
        g_source_remove(recorder->scheduled_event_source);
        recorder->scheduled_event_source = 0;
    }
}

TimedEvent *timed_event_new(TimedEventType type, guint32 group_id, time_t event_time)
{
    fprintf(stderr, "new timed event: %u @ %" G_GUINT64_FORMAT "\n", type, (guint64)event_time);
    TimedEvent *event = NULL;
    switch (type) {
        case TIMED_EVENT_TUNE_IN:
            event = g_malloc(sizeof(TimedEventTuneIn));
            break;
        case TIMED_EVENT_RECORD_START:
            event = g_malloc(sizeof(TimedEventRecordStart));
            break;
        case TIMED_EVENT_RECORD_STOP:
            event = g_malloc(sizeof(TimedEventRecordStop));
            break;
        default:
            return NULL;
    }

    event->type = type;
    event->group_id = group_id;
    event->event_time = event_time;

    return event;
}
