#pragma once

#include <glib.h>

#include "channels.h"
#include "events.h"
#include "epg.h"
#include "streaminfo.h"
#include "filter.h"

typedef struct _DVBRecorder DVBRecorder;

typedef enum {
    TS_TABLE_PAT = 0,
    TS_TABLE_PMT,
    TS_TABLE_EIT,
    TS_TABLE_SDT,
    TS_TABLE_RST,
    N_TS_TABLE_TYPES
} DVBRecorderTSTableType;

typedef struct {
    DVBRecordStatus status;
    gdouble elapsed_time;
    gsize  filesize;
} DVBRecorderRecordStatus;

DVBRecorder *dvb_recorder_new(DVBRecorderEventCallback cb, gpointer userdata);
void dvb_recorder_destroy(DVBRecorder *recorder);

int dvb_recorder_enable_video_source(DVBRecorder *recorder, gboolean enable);
void dvb_recorder_video_source_run(DVBRecorder *recorder);

typedef void (*DVBRecorderLoggerProc)(gchar *, gpointer);
void dvb_recorder_set_logger(DVBRecorder *recorder, DVBRecorderLoggerProc logger, gpointer userdata);
gboolean dvb_recorder_get_logger(DVBRecorder *recorder, DVBRecorderLoggerProc *logger, gpointer *userdata);

GList *dvb_recorder_get_channel_list(DVBRecorder *recorder);
gboolean dvb_recorder_set_channel(DVBRecorder *recorder, guint64 channel_id);
gboolean dvb_recorder_record_start(DVBRecorder *recorder);
void dvb_recorder_record_stop(DVBRecorder *recorder);
void dvb_recorder_stop(DVBRecorder *recorder);
void dvb_recorder_set_capture_dir(DVBRecorder *recorder, const gchar *capture_dir);
void dvb_recorder_set_record_filename_pattern(DVBRecorder *recorder, const gchar *pattern);
gchar *dvb_recorder_make_record_filename(DVBRecorder *recorder, const gchar *alternate_dir, const gchar *alternate_pattern);
void dvb_recorder_query_record_status(DVBRecorder *recorder, DVBRecorderRecordStatus *status);

GList *dvb_recorder_get_epg(DVBRecorder *recorder);
EPGEvent *dvb_recorder_get_epg_event(DVBRecorder *recorder, guint16 event_id);

DVBRecorderEvent *dvb_recorder_event_new(DVBRecorderEventType type, ...);
DVBRecorderEvent *dvb_recorder_event_new_valist(DVBRecorderEventType type, va_list ap);

void dvb_recorder_event_set_property(DVBRecorderEvent *event, const gchar *prop_name, const gpointer prop_value);
void dvb_recorder_event_destroy(DVBRecorderEvent *event);

DVBStreamInfo *dvb_recorder_get_stream_info(DVBRecorder *recorder);
DVBStreamStatus dvb_recorder_get_stream_status(DVBRecorder *recorder);

void dvb_recorder_set_record_filter(DVBRecorder *recorder, DVBFilterType filter);
DVBFilterType dvb_recorder_get_record_filter(DVBRecorder *recorder);

float dvb_recorder_get_signal_strength(DVBRecorder *recorder);

void dvb_recorder_enable_scheduled_events(DVBRecorder *recorder, gboolean enable);
gboolean dvb_recorder_scheduled_events_enabled(DVBRecorder *recorder);

