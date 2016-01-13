#pragma once

#include <glib.h>

#include "dvbrecorder-channel.h"
#include "dvbrecorder-event.h"

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

GList *dvb_recorder_get_channel_list(DVBRecorder *recorder);
gboolean dvb_recorder_set_channel(DVBRecorder *recorder, guint64 channel_id);
gboolean dvb_recorder_record_start(DVBRecorder *recorder, const gchar *filename);
void dvb_recorder_record_stop(DVBRecorder *recorder);
void dvb_recorder_query_record_status(DVBRecorder *recorder, DVBRecorderRecordStatus *status);

DVBRecorderEvent *dvb_recorder_event_new(DVBRecorderEventType type, ...);
DVBRecorderEvent *dvb_recorder_event_new_valist(DVBRecorderEventType type, va_list ap);

void dvb_recorder_event_set_property(DVBRecorderEvent *event, const gchar *prop_name, const gpointer prop_value);
void dvb_recorder_event_destroy(DVBRecorderEvent *event);

void dvb_recorder_event_send(struct _DVBRecorder *recorder, DVBRecorderEventType type, ...);

