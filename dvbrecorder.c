#include <unistd.h>
#include <stdio.h>
#include "dvbrecorder.h"
#include "dvbreader.h"
#include "dvbrecorder-event.h"

struct _DVBRecorder {
    DVBRecorderEventCallback event_cb;
    gpointer event_data;

    gboolean video_source_enabled;

    DVBReader *reader;

    int video_pipe[2];
};

void dvb_recorder_event_callback(DVBRecorder *recorder, DVBRecorderEvent *event, gpointer userdata)
{
}

DVBRecorder *dvb_recorder_new(DVBRecorderEventCallback cb, gpointer userdata)
{
    DVBRecorder *recorder = g_malloc0(sizeof(DVBRecorder));

    recorder->event_cb = cb;
    recorder->event_data = userdata;

    recorder->reader = dvb_reader_new(dvb_recorder_event_callback, NULL);

    return recorder;
}

void dvb_recorder_destroy(DVBRecorder *recorder)
{
    if (!recorder)
        return;
    if (recorder->video_pipe[0] >= 0)
        close(recorder->video_pipe[0]);
    if (recorder->video_pipe[1] >= 0)
        close(recorder->video_pipe[1]);
    dvb_reader_destroy(recorder->reader);

    g_free(recorder);
}

int dvb_recorder_enable_video_source(DVBRecorder *recorder, gboolean enable)
{
    g_return_val_if_fail(recorder != NULL, -1);

    if (recorder->video_source_enabled == enable)
        return recorder->video_pipe[0];

    recorder->video_source_enabled = enable;
    if (enable) {
        pipe(recorder->video_pipe);
        /* for decoding (gstreamer) pat and pmt are necessary */
        dvb_reader_set_listener(recorder->reader,
                DVB_FILTER_VIDEO | DVB_FILTER_AUDIO | DVB_FILTER_TELETEXT |
                DVB_FILTER_SUBTITLES | DVB_FILTER_PAT | DVB_FILTER_PMT/* | DVB_FILTER_UNKNOWN*/,
                recorder->video_pipe[1], NULL, NULL);

        return recorder->video_pipe[0];
    }
    else {
        dvb_reader_remove_listener(recorder->reader, recorder->video_pipe[1], NULL);
        close(recorder->video_pipe[0]);
        close(recorder->video_pipe[1]);
        recorder->video_pipe[0] = -1;
        recorder->video_pipe[1] = -1;

        return -1;
    }
}

GList *dvb_recorder_get_channel_list(DVBRecorder *recorder)
{
    return NULL;
}

gboolean dvb_recorder_set_channel(DVBRecorder *recorder, guint64 channel_id)
{
    dvb_reader_tune(recorder->reader, 0, 0, 0, 0, 28721);

    return TRUE;
}
