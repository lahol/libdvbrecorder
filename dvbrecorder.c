#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "dvbrecorder.h"
#include "dvbreader.h"
#include "events.h"
#include "read-ts.h"
#include "channels.h"
#include "channel-db.h"

struct _DVBRecorder {
    DVBRecorderEventCallback event_cb;
    gpointer event_data;

    gboolean video_source_enabled;

    DVBReader *reader;

    int video_pipe[2];

    int record_fd;
    gchar *record_filename;
    DVBRecordStatus record_status;
    time_t record_start;
    time_t record_end;             /* keep data if stream was stopped, for last info */
    gsize record_size;
};

void dvb_recorder_event_callback(DVBRecorderEvent *event, gpointer userdata)
{
    DVBRecorder *recorder = (DVBRecorder *)userdata;
    if (!recorder || !recorder->event_cb)
        return;
    switch (event->type) {
        case DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED:
            recorder->event_cb(event, recorder->event_data);
            break;
        default:
            break;
    }
}

DVBRecorder *dvb_recorder_new(DVBRecorderEventCallback cb, gpointer userdata)
{
    DVBRecorder *recorder = g_malloc0(sizeof(DVBRecorder));

    recorder->event_cb = cb;
    recorder->event_data = userdata;

    recorder->reader = dvb_reader_new(dvb_recorder_event_callback, recorder);
    if (!recorder->reader)
        goto err;

    return recorder;

err:
    dvb_recorder_destroy(recorder);
    return NULL;
}

void dvb_recorder_destroy(DVBRecorder *recorder)
{
    fprintf(stderr, "dvb_recorder_destroy\n");
    if (!recorder)
        return;
    if (recorder->video_pipe[0] >= 0)
        close(recorder->video_pipe[0]);
    if (recorder->video_pipe[1] >= 0)
        close(recorder->video_pipe[1]);

    g_free(recorder->record_filename);

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
        if (pipe(recorder->video_pipe) != 0) {
            fprintf(stderr, "pipe failed: (%d) %s\n", errno, strerror(errno));
        }
        fprintf(stderr, "video_pipe: %d/%d, reader: %p\n", recorder->video_pipe[0], recorder->video_pipe[1], recorder->reader);
        /* for decoding (gstreamer) pat and pmt are necessary */
        dvb_reader_set_listener(recorder->reader,
                DVB_FILTER_VIDEO | DVB_FILTER_AUDIO | DVB_FILTER_TELETEXT |
                DVB_FILTER_SUBTITLES | DVB_FILTER_PAT | DVB_FILTER_PMT/* | DVB_FILTER_UNKNOWN*/,
                recorder->video_pipe[1], NULL, NULL);

        fprintf(stderr, "video_pipe: %d\n", recorder->video_pipe[0]);
        return recorder->video_pipe[0];
    }
    else {
        fprintf(stderr, "[lib] enable_video_source: FALSE\n");
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
    g_return_val_if_fail(recorder != NULL, FALSE);

    ChannelData *chdata = channel_db_get_channel(channel_id);

    if (chdata) {
        dvb_reader_tune(recorder->reader,
                        chdata->frequency * 1000, /* frequency */
                        chdata->polarization == CHNL_POLARIZATION_HORIZONTAL ? 1 : 0,  /* polarization */
                        0,                        /* sat number */
                        chdata->srate * 1000,     /* symbol rate */
                        28721);                   /* program number */
        return TRUE;
    }
    else {
        return FALSE;
    }
}

void dvb_recorder_record_callback(const guint8 *packet, DVBReaderFilterType type, DVBRecorder *recorder)
{
    ssize_t nw, offset;
    for (offset = 0; offset < TS_SIZE; offset += nw) {
        if ((nw = write(recorder->record_fd, packet + offset, (size_t)(TS_SIZE - offset))) <= 0) {
            if (nw < 0) {
                fprintf(stderr, "Could not write. Stop recording: %d (%s)\n", errno, strerror(errno));
                goto err;
            }
            break;
        }
    }

    recorder->record_size += TS_SIZE;

    return;
err:
    dvb_recorder_record_stop(recorder);
}

gboolean dvb_recorder_record_start(DVBRecorder *recorder, const gchar *filename)
{
    g_return_val_if_fail(recorder != NULL, FALSE);
    g_return_val_if_fail(filename != NULL && filename[0] != '\0', FALSE);

    if (recorder->record_status == DVB_RECORD_STATUS_RECORDING) {
        fprintf(stderr, "Already recording\n");
        return FALSE;
    }

    /* Query reader status first and return error if not tuned in? */
    if (dvb_reader_get_stream_status(recorder->reader) != DVB_STREAM_STATUS_RUNNING) {
        fprintf(stderr, "Stream not running.\n");
        return FALSE;
    }

    /* in extra function: allow placeholders in (user-defined) filename:
     * %{station_name}, %{station_provider}, %{date:%Y%m%d} */
    gchar *provider = NULL, *name = NULL;
    guint8 type;
    if (dvb_reader_get_stream_info(recorder->reader, &provider, &name, &type)) {
        fprintf(stderr, "Provider: %s\nName: %s\nType: %d\n",
                provider, name, type);
        g_free(provider);
        g_free(name);
    }

    recorder->record_fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (recorder->record_fd == -1) {
        fprintf(stderr, "Failed to open %s: (%d) %s\n", filename, errno, strerror(errno));
        return FALSE;
    }

    if (recorder->record_filename)
        g_free(recorder->record_filename);
    recorder->record_filename = g_strdup(filename);
    recorder->record_size = 0;
    time(&recorder->record_start);
    recorder->record_status = DVB_RECORD_STATUS_RECORDING;

    /* FIXME: take filter from config */
    dvb_reader_set_listener(recorder->reader, DVB_FILTER_ALL, -1,
            (DVBReaderListenerCallback)dvb_recorder_record_callback, recorder);

    dvb_recorder_event_send(DVB_RECORDER_EVENT_RECORD_STATUS_CHANGED,
            recorder->event_cb, recorder->event_data,
            "status", DVB_RECORD_STATUS_RECORDING,
            NULL, NULL);
    return TRUE;
}

void dvb_recorder_record_stop(DVBRecorder *recorder)
{
    g_return_if_fail(recorder != NULL);

    dvb_reader_remove_listener(recorder->reader, -1, (DVBReaderListenerCallback)dvb_recorder_record_callback);
    fprintf(stderr, "[lib] dvb_recorder_record_stop, record_fd: %d\n", recorder->record_fd);
    if (recorder->record_fd >= 0) {
        close(recorder->record_fd);
        recorder->record_fd = -1;
    }
    recorder->record_status = DVB_RECORD_STATUS_STOPPED;
    time(&recorder->record_end);

    dvb_recorder_event_send(DVB_RECORDER_EVENT_RECORD_STATUS_CHANGED,
            recorder->event_cb, recorder->event_data,
            "status", DVB_RECORD_STATUS_STOPPED,
            NULL, NULL);
}

void dvb_recorder_query_record_status(DVBRecorder *recorder, DVBRecorderRecordStatus *status)
{
    g_return_if_fail(recorder != NULL);
    g_return_if_fail(status != NULL);

    status->filesize = recorder->record_size;
    status->status = recorder->record_status;

    time_t end;
    if (recorder->record_status == DVB_RECORD_STATUS_RECORDING)
        time(&end);
    else
        end = recorder->record_end;

    status->elapsed_time = difftime(end, recorder->record_start);
}
