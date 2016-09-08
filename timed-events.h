#pragma once

#include <glib.h>
#include <time.h>
#include "dvbrecorder.h"

typedef enum {
    TIMED_EVENT_INVALID_TYPE = 0,
    TIMED_EVENT_TUNE_IN,
    TIMED_EVENT_RECORD_START,
    TIMED_EVENT_RECORD_STOP
} TimedEventType;

typedef struct {
    TimedEventType type;
    guint32 group_id;
    time_t event_time;
} TimedEvent;

typedef struct {
    TimedEvent parent;
    guint64    channel_id;
} TimedEventTuneIn;

typedef struct {
    TimedEvent parent;
} TimedEventRecordStart;

typedef struct {
    TimedEvent parent;
} TimedEventRecordStop;

/* see cinet.h */
/*void dvb_recorder_add_timed_event(DVBRecorder *recorder, TimedEventType type, guint32 group_id, time_t event_time, ...);*/

TimedEvent *timed_event_new(TimedEventType type, guint32 group_id, time_t event_time);
void dvb_recorder_add_timed_event(DVBRecorder *recorder, TimedEvent *event);
/* to be called every 60 seconds to check if the next timed event should be run soon */
gboolean dvb_recorder_check_timed_events(DVBRecorder *recorder);
void dvb_recorder_timed_events_clear(DVBRecorder *recorder);
void dvb_recorder_timed_events_remove_group(DVBRecorder *recorder, guint32 group_id);
