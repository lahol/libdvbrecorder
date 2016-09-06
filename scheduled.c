#include "scheduled.h"
#include "dvbrecorder-internal.h"
#include "timed-events.h"
#include <sqlite3.h>
#include <stdio.h>

extern sqlite3 *dbhandler_db;

sqlite3_stmt *add_event_stmt = NULL;
sqlite3_stmt *remove_event_stmt = NULL;
sqlite3_stmt *enum_event_stmt = NULL;
sqlite3_stmt *find_upcoming_events_stmt = NULL;

void dvb_recorder_translate_scheduled_event(DVBRecorder *recorder, ScheduledEvent *event);
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
    if (remove_event_stmt) {
        sqlite3_finalize(remove_event_stmt);
        remove_event_stmt = NULL;
    }
    if (enum_event_stmt) {
        sqlite3_finalize(enum_event_stmt);
        enum_event_stmt = NULL;
    }
    if (find_upcoming_events_stmt) {
        sqlite3_finalize(find_upcoming_events_stmt);
        find_upcoming_events_stmt = NULL;
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

    guint last_id = (guint)sqlite3_last_insert_rowid(dbhandler_db);

    if (time_end > time(NULL)) {
        ScheduledEvent event;
        event.id = last_id;
        event.channel_id = channel_id;
        event.time_start = time_start;
        event.time_end   = time_end;
        event.recurring_parent = 0;
        event.status = 0;

        dvb_recorder_translate_scheduled_event(recorder, &event);
    }

    return last_id;
}

void scheduled_event_remove(DVBRecorder *recorder, guint event_id)
{
    g_return_if_fail(recorder != NULL);
    g_return_if_fail(event_id != 0);

    int rc;

    if (remove_event_stmt == NULL) {
        rc = sqlite3_prepare_v2(dbhandler_db,
                "delete from schedule_events where event_id=?;",
                -1, &remove_event_stmt, NULL);
        if (rc != SQLITE_OK)
            return;
    }

    sqlite3_bind_int64(remove_event_stmt, 1, (gint64)event_id);

    rc = sqlite3_step(remove_event_stmt);
    sqlite3_reset(remove_event_stmt);

    dvb_recorder_enable_scheduled_events(recorder, recorder->scheduled_recordings_enabled);
}

guint scheduled_event_add_recurring(DVBRecorder *recorder, guint channel_id, ScheduleWeekday weekday, guint start_time, guint duration)
{
    return 0;
}

void scheduled_event_enum(ScheduledEventEnumProc callback, gpointer userdata)
{
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

void scheduled_event_recurring_enum(ScheduledEventRecurringEnumProc callback, gpointer userdata)
{
}

ScheduledEvent *scheduled_event_get(DVBRecorder *recorder, guint id)
{
    return NULL;
}

void dvb_recorder_translate_scheduled_event(DVBRecorder *recorder, ScheduledEvent *event)
{
    TimedEvent *timed = NULL;

    /* tune in 60 seconds before recording */
    timed = timed_event_new(TIMED_EVENT_TUNE_IN, event->id, event->time_start - 60);
    ((TimedEventTuneIn *)timed)->channel_id = event->channel_id;

    dvb_recorder_add_timed_event(recorder, timed);

    /* record start */
    timed = timed_event_new(TIMED_EVENT_RECORD_START, event->id, event->time_start);
    dvb_recorder_add_timed_event(recorder, timed);

    timed = timed_event_new(TIMED_EVENT_RECORD_STOP, event->id, event->time_end);
    dvb_recorder_add_timed_event(recorder, timed);
}

void dvb_recorder_find_upcoming_scheduled_events(DVBRecorder *recorder)
{
    g_return_if_fail(recorder != NULL);

    int rc;
    ScheduledEvent event;

    if (find_upcoming_events_stmt == NULL) {
        rc = sqlite3_prepare_v2(dbhandler_db, "select * from schedule_events where event_start > ? order by event_start asc",
                -1, &find_upcoming_events_stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Could not create find upcoming scheduled event statement.\n");
            return;
        }
    }

    sqlite3_bind_int64(find_upcoming_events_stmt, 1, (gint64)time(NULL));

    while ((rc = sqlite3_step(find_upcoming_events_stmt)) == SQLITE_ROW) {
        event.id = (guint)sqlite3_column_int(find_upcoming_events_stmt, 0);
        event.time_start = (guint64)sqlite3_column_int64(find_upcoming_events_stmt, 1);
        event.time_end = (guint64)sqlite3_column_int64(find_upcoming_events_stmt, 2);
        event.channel_id = (guint)sqlite3_column_int64(find_upcoming_events_stmt, 3);
        event.status = sqlite3_column_int(find_upcoming_events_stmt, 4);
        event.recurring_parent = sqlite3_column_int(find_upcoming_events_stmt, 5);

        dvb_recorder_translate_scheduled_event(recorder, &event);
    }

    sqlite3_reset(find_upcoming_events_stmt);
}

void dvb_recorder_enable_scheduled_events(DVBRecorder *recorder, gboolean enable)
{
    g_return_if_fail(recorder != NULL);

    dvb_recorder_timed_events_clear(recorder);

    recorder->scheduled_recordings_enabled = enable ? 1 : 0;

    if (enable) {
        dvb_recorder_find_upcoming_scheduled_events(recorder);
        if (!recorder->check_timed_events_timer_source) {
            recorder->check_timed_events_timer_source =
                g_timeout_add_seconds(30, (GSourceFunc)dvb_recorder_check_timed_events, recorder);
        }
    }
    else if (recorder->check_timed_events_timer_source) {
        g_source_remove(recorder->check_timed_events_timer_source);
        recorder->check_timed_events_timer_source = 0;
    }
}

gboolean dvb_recorder_scheduled_events_enabled(DVBRecorder *recorder)
{
    g_return_val_if_fail(recorder != NULL, FALSE);

    return (gboolean)recorder->scheduled_recordings_enabled;
}
