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

DVBRecorder *dvb_recorder_new(DVBRecorderEventCallback cb, gpointer userdata);
void dvb_recorder_destroy(DVBRecorder *recorder);

int dvb_recorder_enable_video_source(DVBRecorder *recorder, gboolean enable);

GList *dvb_recorder_get_channel_list(DVBRecorder *recorder);
gboolean dvb_recorder_set_channel(DVBRecorder *recorder, guint64 channel_id);

DVBRecorderEvent *dvb_recorder_event_new(DVBRecorderEventType type, ...);
void dvb_recorder_event_set_property(DVBRecorderEvent *event, const gchar *prop_name, const gpointer prop_value);
void dvb_recorder_event_destroy(DVBRecorderEvent *event);
