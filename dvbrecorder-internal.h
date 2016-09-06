#pragma once

#include "dvbrecorder.h"
#include "scheduled.h"
#include "dvbreader.h"

struct _DVBRecorder {
    DVBRecorderEventCallback event_cb;
    gpointer event_data;

    DVBRecorderLoggerProc logger;
    gpointer logger_data;

    gboolean video_source_enabled;

    DVBReader *reader;

    int video_pipe[2];

    guint64 current_channel_id;

    int record_fd;
    gchar *record_filename;
    gchar *record_filename_pattern;
    gchar *capture_dir;
    DVBRecordStatus record_status;
    time_t record_start;
    time_t record_end;             /* keep data if stream was stopped, for last info */
    gsize record_size;
    DVBFilterType record_filter;

    guint scheduled_recordings_enabled : 1;

    GList *timed_events;
    guint scheduled_event_source;

    guint check_timed_events_timer_source;
};
