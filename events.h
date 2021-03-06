#pragma once

#include <glib.h>

typedef enum {
    DVB_RECORDER_EVENT_TUNED = 0,
    DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED,
    DVB_RECORDER_EVENT_SOURCE_FD_CHANGED,
    DVB_RECORDER_EVENT_TUNE_IN,
    DVB_RECORDER_EVENT_STOP_THREAD,
    DVB_RECORDER_EVENT_RECORD_STATUS_CHANGED,
    DVB_RECORDER_EVENT_EIT_CHANGED,
    DVB_RECORDER_EVENT_SDT_CHANGED,
    DVB_RECORDER_EVENT_LISTENER_STATUS_CHANGED,
    DVB_RECORDER_EVENT_VIDEO_DIED,
    DVB_RECORDER_EVENT_CHANNEL_CHANGED,
    DVB_RECORDER_EVENT_COUNT
} DVBRecorderEventType;

typedef enum {
    DVB_STREAM_STATUS_UNKNOWN = 0,
    DVB_STREAM_STATUS_TUNED,
    DVB_STREAM_STATUS_TUNE_FAILED,
    DVB_STREAM_STATUS_RUNNING,
    DVB_STREAM_STATUS_STOPPED,
    DVB_STREAM_STATUS_EOS
} DVBStreamStatus;

typedef enum {
    DVB_RECORD_STATUS_UNKNOWN = 0,
    DVB_RECORD_STATUS_RECORDING,
    DVB_RECORD_STATUS_STOPPED
} DVBRecordStatus;

typedef enum {
    DVB_LISTENER_STATUS_UNKNOWN = 0,
    DVB_LISTENER_STATUS_EOS,
    DVB_LISTENER_STATUS_TERMINATED,
    DVB_LISTENER_STATUS_WRITE_ERROR
} DVBListenerStatus;

typedef struct {
    DVBRecorderEventType type;
} DVBRecorderEvent;

typedef struct {
    DVBRecorderEvent parent;
    int fd;
} DVBRecorderEventTuned;

typedef struct {
    DVBRecorderEvent parent;
    DVBStreamStatus status;
} DVBRecorderEventStreamStatusChanged;

typedef struct {
    DVBRecorderEvent parent;
    int fd;
} DVBRecorderEventSourceFdChanged;

typedef struct {
    DVBRecorderEvent parent;

    guint32 frequency;
    guint8 polarization;
    guint8 sat_no;
    guint8 delivery_system;
    guint16 modulation;
    guint16 roll_off;
    guint32 symbol_rate;
    guint16 program_number;
} DVBRecorderEventTuneIn;

typedef struct {
    DVBRecorderEvent parent;
} DVBRecorderEventStopThread;

typedef struct {
    DVBRecorderEvent parent;
    DVBRecordStatus status;
} DVBRecorderEventRecordStatusChanged;

typedef struct {
    DVBRecorderEvent parent;

    guint8 table_id;
} DVBRecorderEventEITChanged;

typedef struct {
    DVBRecorderEvent parent;
} DVBRecorderEventSDTChanged;

typedef struct {
    DVBRecorderEvent parent;

    guint status;
    gint listener_fd;
    gpointer listener_cb;

    guint fd_valid : 1;
    guint cb_valid : 1;
} DVBRecorderEventListenerStatusChanged;

typedef struct {
    DVBRecorderEvent parent;
} DVBRecorderEventVideoDied;

typedef struct {
    DVBRecorderEvent parent;
    guint channel_id;
} DVBRecorderEventChannelChanged;

typedef void (*DVBRecorderEventCallback)(DVBRecorderEvent *, gpointer);
void dvb_recorder_event_send(DVBRecorderEventType type, DVBRecorderEventCallback cb, gpointer data, ...);
