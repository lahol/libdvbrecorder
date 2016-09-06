#include "scheduled.h"
#include <sqlite3.h>
#include <stdio.h>

extern sqlite3 *dbhandler_db;

sqlite3_stmt *add_event_stmt = NULL;
sqlite3_stmt *enum_event_stmt = NULL;
sqlite3_stmt *find_next_event_stmt = NULL;

void scheduled_events_check_next_event(DVBRecorder *recorder);

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

void scheduled_events_db_cleanup(void)
{
    if (add_event_stmt) {
        sqlite3_finalize(add_event_stmt);
        add_event_stmt = NULL;
    }
    if (enum_event_stmt) {
        sqlite3_finalize(enum_event_stmt);
        enum_event_stmt = NULL;
    }
    if (find_next_event_stmt) {
        sqlite3_finalize(find_next_event_stmt);
        find_next_event_stmt = NULL;
    }
}

guint scheduled_event_add(DVBRecorder *recorder, guint channel_id, guint64 time_start, guint64 time_end)
{
    g_return_val_if_fail(recorder != NULL, 0);

    int rc;

    if (add_event_stmt == NULL) {
        rc = sqlite3_prepare_v2(dbhandler_db,
                "insert into schedule_events(event_start, event_end, chnl_id, status, recurring_parent) "
                "values (?,?,?,?,?);",
                -1, &add_event_stmt, NULL);
        if (rc != SQLITE_OK)
            return 0;
    }

    /* fixme: take care of signedness */
    sqlite3_bind_int64(add_event_stmt, 1, (gint64)time_start);
    sqlite3_bind_int64(add_event_stmt, 2, (gint64)time_end);
    sqlite3_bind_int64(add_event_stmt, 3, (gint64)channel_id);
    sqlite3_bind_int(add_event_stmt, 4, 0);
    sqlite3_bind_int(add_event_stmt, 5, 0);

    rc = sqlite3_step(add_event_stmt);
    sqlite3_reset(add_event_stmt);

    if (rc != SQLITE_OK && rc != SQLITE_DONE)
        return 0;

    return (guint)sqlite3_last_insert_rowid(dbhandler_db);
}

guint scheduled_event_add_recurring(DVBRecorder *recorder, guint channel_id, ScheduleWeekday weekday, guint start_time, guint duration)
{
    return 0;
}

void scheduled_event_enum(DVBRecorder *recorder, ScheduledEventEnumProc callback, gpointer userdata)
{
    g_return_if_fail(recorder != NULL);
    g_return_if_fail(callback != NULL);

    int rc;
    ScheduledEvent event;

    if (enum_event_stmt == NULL) {
        rc = sqlite3_prepare_v2(dbhandler_db, "select * from schedule_events order by event_start asc",
                -1, &enum_event_stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Could not create enum events statement.\n");
            return;
        }
    }

    while ((rc = sqlite3_step(enum_event_stmt)) == SQLITE_ROW) {
        event.id = (guint)sqlite3_column_int(enum_event_stmt, 0);
        event.time_start = (guint64)sqlite3_column_int64(enum_event_stmt, 1);
        event.time_end = (guint64)sqlite3_column_int64(enum_event_stmt, 2);
        event.channel_id = (guint)sqlite3_column_int64(enum_event_stmt, 3);
        event.status = sqlite3_column_int(enum_event_stmt, 4);
        event.recurring_parent = sqlite3_column_int(enum_event_stmt, 5);

        callback(&event, userdata);
    }

    sqlite3_reset(enum_event_stmt);
}

void scheduled_event_recurring_enum(DVBRecorder *recorder, ScheduledEventRecurringEnumProc callback, gpointer userdata)
{
}

ScheduledEvent *scheduled_event_get(DVBRecorder *recorder, guint id)
{
    return NULL;
}

void dvb_recorder_find_next_scheduled_event(DVBRecorder *recorder)
{
    g_return_if_fail(recorder != NULL);

    int rc;
    ScheduledEvent event;

    if (find_next_event_stmt == NULL) {
        rc = sqlite3_prepare_v2(dbhandler_db, "select * from schedule_events where event_end > ? order by event_start asc limit 1,1",
                -1, &find_next_event_stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Could not create find next scheduled event statement.\n");
            return;
        }
    }

    sqlite3_bind_int64(find_next_event_stmt, 1, (gint64)time(NULL));

    rc = sqlite3_step(find_next_event_stmt);
    if (rc == SQLITE_ROW) {
        event.id = (guint)sqlite3_column_int(find_next_event_stmt, 0);
        event.time_start = (guint64)sqlite3_column_int64(find_next_event_stmt, 1);
        event.time_end = (guint64)sqlite3_column_int64(find_next_event_stmt, 2);
        event.channel_id = (guint)sqlite3_column_int64(find_next_event_stmt, 3);
        event.status = sqlite3_column_int(find_next_event_stmt, 4);
        event.recurring_parent = sqlite3_column_int(find_next_event_stmt, 5);

        dvb_recorder_set_next_scheduled_event(recorder, &event);
    }

    sqlite3_reset(find_next_event_stmt);
}
