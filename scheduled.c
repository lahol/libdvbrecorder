#include "scheduled.h"
#include <sqlite3.h>

extern sqlite3 *dbhandler_db;

gint scheduled_events_db_init(void)
{
    /* init statements */
    return 0;
}

guint scheduled_event_add(DVBRecorder *recorder, guint channel_id, guint64 time_start, guint64 time_end)
{
    return 0;
}

guint scheduled_event_add_recurring(DVBRecorder *recorder, guint channel_id, ScheduleWeekday weekday, guint start_time, guint duration)
{
    return 0;
}

void scheduled_event_enum(DVBRecorder *recorder, ScheduledEventEnumProc callback, gpointer userdata)
{
}

void scheduled_event_recurring_enum(DVBRecorder *recorder, ScheduledEventRecurringEnumProc callback, gpointer userdata)
{
}

ScheduledEvent *scheduled_event_get(DVBRecorder *recorder, guint id)
{
    return NULL;
}
