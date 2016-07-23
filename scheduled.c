#include "scheduled.h"
#include <sqlite3.h>

extern sqlite3 *dbhandler_db;

gint scheduled_events_db_init(void)
{
    /* This gets only called from channel_db_init. Thus we do not have to check whether dbhandler_db is valid. */
    gint rc;
    char *sql;

    sql = "create table if not exists schedule_events(event_id integer primary key, event_start integer, event_end integer,\
           chnl_id integer, status integer, recurring_parent integer)";
    rc = sqlite3_exec(dbhandler_db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK)
        return 1;

    sql = "create table if not exists schedule_recurring(recurrent_id integer primary key, weekday integer, time_start integer,\
           duration integer, chnl_id integer, next_event_id integer)";
    rc = sqlite3_exec(dbhandler_db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK)
        return 1;

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
