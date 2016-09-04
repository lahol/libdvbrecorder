#pragma once

#include <glib.h>
#include "dvbrecorder.h"

typedef enum {
    SCHEDULED_EVENT_STATUS_UNKNOWN = 0,
    SCHEDULED_EVENT_STATUS_IN_PREPARATION,
    SCHEDULED_EVENT_STATUS_RECORDING,
    SCHEDULED_EVENT_STATUS_CANCELLED,
    SCHEDULED_EVENT_STATUS_DONE
} ScheduledEventStatus;

typedef struct _ScheduledEvent {
    guint id;
    guint channel_id;
    guint64 time_start;
    guint64 time_end;
    ScheduledEventStatus status;
    guint recurring_parent;
} ScheduledEvent;

typedef enum {
    SCHEDULED_NEVER     = 0,
    SCHEDULED_MONDAY    = 1 << 0,
    SCHEDULED_TUESDAY   = 1 << 1,
    SCHEDULED_WEDNESDAY = 1 << 2,
    SCHEDULED_THURSDAY  = 1 << 3,
    SCHEDULED_FRIDAY    = 1 << 4,
    SCHEDULED_SATURDAY  = 1 << 5,
    SCHEDULED_SUNDAY    = 1 << 6,
    SCHEDULED_WEEKDAYS  = SCHEDULED_MONDAY | SCHEDULED_TUESDAY |
                          SCHEDULED_WEDNESDAY | SCHEDULED_THURSDAY | SCHEDULED_FRIDAY,
    SCHEDULED_WEEKEND   = SCHEDULED_SATURDAY | SCHEDULED_SUNDAY,
    SCHEDULED_EVERYDAY  = SCHEDULED_WEEKDAYS | SCHEDULED_WEEKEND
} ScheduleWeekday;

typedef struct _ScheduledEventRecurring {
    guint id;

    guint channel_id;
    ScheduleWeekday weekday;
    guint start_time;
    guint duration;

    guint next_event_id;
} ScheduledEventRecurring;

gint scheduled_events_db_init(void);
void scheduled_events_db_cleanup(void);

guint scheduled_event_add(DVBRecorder *recorder, guint channel_id, guint64 time_start, guint64 time_end);
guint scheduled_event_add_recurring(DVBRecorder *recorder, guint channel_id, ScheduleWeekday weekday, guint start_time, guint duration);

typedef void (*ScheduledEventEnumProc)(ScheduledEvent *, gpointer);
void scheduled_event_enum(DVBRecorder *recorder, ScheduledEventEnumProc callback, gpointer userdata);

typedef void (*ScheduledEventRecurringEnumProc)(ScheduledEventRecurring *, gpointer);
void scheduled_event_recurring_enum(DVBRecorder *recorder, ScheduledEventRecurringEnumProc callback, gpointer userdata);

ScheduledEvent *scheduled_event_get(DVBRecorder *recorder, guint id);

