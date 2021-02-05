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
#include "logging.h"
#include "scheduled.h"
#include "dvbrecorder-internal.h"
#include "timed-events.h"

void dvb_recorder_event_callback(DVBRecorderEvent *event, gpointer userdata)
{
    FLOG("\n");
    DVBRecorder *recorder = (DVBRecorder *)userdata;
    if (!recorder || !recorder->event_cb)
        return;
    switch (event->type) {
        case DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED:
        case DVB_RECORDER_EVENT_EIT_CHANGED:
        case DVB_RECORDER_EVENT_SDT_CHANGED:
        case DVB_RECORDER_EVENT_CHANNEL_CHANGED:
            recorder->event_cb(event, recorder->event_data);
            break;
        case DVB_RECORDER_EVENT_LISTENER_STATUS_CHANGED:
            {
                LOG(&recorder->logger, "EVENT_LISTENER_STATUS_CHANGED\n");
                DVBRecorderEventListenerStatusChanged *ev = (DVBRecorderEventListenerStatusChanged *)event;
                LOG(&recorder->logger, "status: %d, fd: %d, cb: %p\n", ev->status, ev->listener_fd, ev->listener_cb);
                if (ev->status == DVB_LISTENER_STATUS_EOS) {
                    if (ev->fd_valid && ev->listener_fd == recorder->video_pipe[1] && recorder->video_source_enabled) {
                        dvb_recorder_event_send(DVB_RECORDER_EVENT_STREAM_STATUS_CHANGED,
                                recorder->event_cb, recorder->event_data,
                                "status", DVB_STREAM_STATUS_STOPPED,
                                NULL, NULL);
                    }
                }
                else if (ev->status == DVB_LISTENER_STATUS_TERMINATED) {
                    LOG(&recorder->logger, "listener terminated, remove it\n");
                    dvb_reader_remove_listener(recorder->reader, ev->fd_valid ? ev->listener_fd : -1,
                                                                 ev->cb_valid ? ev->listener_cb : NULL);
                }
                else if (ev->status == DVB_LISTENER_STATUS_WRITE_ERROR) {
                    if (ev->fd_valid && ev->listener_fd == recorder->video_pipe[1] && recorder->video_source_enabled) {
                        LOG(&recorder->logger, "video write error\n");
                        dvb_recorder_event_send(DVB_RECORDER_EVENT_VIDEO_DIED,
                                recorder->event_cb, recorder->event_data,
                                NULL, NULL);
                    }
                }
            }
            break;
        default:
            break;
    }
}

DVBRecorder *dvb_recorder_new(DVBRecorderEventCallback cb, gpointer userdata)
{
    FLOG("\n");
    DVBRecorder *recorder = g_malloc0(sizeof(DVBRecorder));

    recorder->event_cb = cb;
    recorder->event_data = userdata;

    recorder->video_pipe[0] = -1;
    recorder->video_pipe[1] = -1;
    recorder->record_fd = -1;

    recorder->capture_dir = g_strdup(g_get_home_dir());
    recorder->record_filename_pattern = g_strdup("capture-${date:%Y%m%d-%H%M%S}.ts");

    recorder->record_filter = DVB_FILTER_ALL;

    recorder->reader = dvb_reader_new(dvb_recorder_event_callback, recorder);
    if (!recorder->reader)
        goto err;
    dvb_reader_set_logger(recorder->reader, &recorder->logger);

    return recorder;

err:
    dvb_recorder_destroy(recorder);
    return NULL;
}

void dvb_recorder_destroy(DVBRecorder *recorder)
{
    FLOG("\n");
    LOG(&recorder->logger, "dvb_recorder_destroy\n");
    if (!recorder)
        return;

    dvb_recorder_enable_video_source(recorder, FALSE);
    if (recorder->video_pipe[1] >= 0) {
        close(recorder->video_pipe[1]);
    }

    if (recorder->video_pipe[0] >= 0)
        close(recorder->video_pipe[0]);
    g_free(recorder->record_filename);

    dvb_reader_destroy(recorder->reader);

    dvb_recorder_timed_events_clear(recorder);

    g_free(recorder);
}

void dvb_recorder_set_logger(DVBRecorder *recorder, DVBRecorderLoggerProc logger, gpointer userdata)
{
    FLOG("\n");
    g_return_if_fail(recorder != NULL);

    recorder->logger.log = logger;
    recorder->logger.data = userdata;
}

int dvb_recorder_enable_video_source(DVBRecorder *recorder, gboolean enable)
{
    FLOG("\n");
    LOG(&recorder->logger, "dvb_recorder_enable_video_source: %d\n", enable);
    g_return_val_if_fail(recorder != NULL, -1);

    recorder->video_source_enabled = enable;
    if (enable) {
        if ((recorder->video_pipe[0] == -1 || recorder->video_pipe[1] == -1) && pipe(recorder->video_pipe) != 0) {
            LOG(&recorder->logger, "pipe failed: (%d) %s\n", errno, strerror(errno));
        }
        LOG(&recorder->logger, "video_pipe: %d/%d, reader: %p\n", recorder->video_pipe[0], recorder->video_pipe[1], recorder->reader);
/*        fcntl(recorder->video_pipe[1], F_SETFL, O_NONBLOCK);*/
        /* for decoding (gstreamer) pat and pmt are necessary */
        dvb_reader_set_listener(recorder->reader,
                DVB_FILTER_VIDEO | DVB_FILTER_AUDIO | DVB_FILTER_TELETEXT |
                DVB_FILTER_SUBTITLES | DVB_FILTER_PAT | DVB_FILTER_PMT | DVB_FILTER_PCR/* | DVB_FILTER_UNKNOWN*/,
                recorder->video_pipe[1], NULL, NULL);

        LOG(&recorder->logger, "video_pipe: %d\n", recorder->video_pipe[0]);
        return recorder->video_pipe[0];
    }
    else {
        LOG(&recorder->logger, "enable_video_source: FALSE\n");
        dvb_reader_remove_listener(recorder->reader, recorder->video_pipe[1], NULL);
        close(recorder->video_pipe[0]);
        close(recorder->video_pipe[1]);
        recorder->video_pipe[0] = -1;
        recorder->video_pipe[1] = -1;

        return -1;

    }
}

void dvb_recorder_video_source_run(DVBRecorder *recorder)
{
    g_return_if_fail(recorder != NULL);

    dvb_reader_listener_set_running(recorder->reader, recorder->video_pipe[1], NULL, TRUE);
}

GList *dvb_recorder_get_channel_list(DVBRecorder *recorder)
{
    FLOG("\n");
    return NULL;
}

gboolean dvb_recorder_set_channel(DVBRecorder *recorder, guint64 channel_id)
{
    FLOG("\n");
    g_return_val_if_fail(recorder != NULL, FALSE);

    LOG(&recorder->logger, "dvb_recorder_set_channel %lu -> %lu\n", recorder->current_channel_id, channel_id);
#ifndef DVB_TUNER_DUMMY
    /* Allow same channel in dummy mode */
    if (recorder->current_channel_id == channel_id)
        return TRUE;
#endif

    ChannelData *chdata = channel_db_get_channel(channel_id);

    if (chdata) {
        /* stop running recording first */
        if (recorder->record_status == DVB_RECORD_STATUS_RECORDING)
            dvb_recorder_record_stop(recorder);

        LOG(&recorder->logger, "dvbrecorder.c: dvb_reader_tune: chdata->polarization: %d\n", chdata->polarization);
        dvb_reader_tune(recorder->reader,
                        chdata->frequency,        /* frequency */
                        chdata->polarization == CHNL_POLARIZATION_HORIZONTAL ? 1 : 0,  /* polarization */
                        0,                        /* sat number */
                        chdata->srate,            /* symbol rate */
                        chdata->delivery_system,  /* delivery system */
                        chdata->modulation,       /* modulation */
                        chdata->roll_off,         /* roll off */
                        chdata->sid);             /* program number */

        recorder->current_channel_id = channel_id;

        dvb_recorder_event_send(DVB_RECORDER_EVENT_CHANNEL_CHANGED,
                recorder->event_cb, recorder->event_data,
                "channel-id", channel_id,
                NULL, NULL);

        return TRUE;
    }
    else {
        return FALSE;
    }
}

guint64 dvb_recorder_get_current_channel(DVBRecorder *recorder)
{
    g_return_val_if_fail(recorder != NULL, 0);

    return recorder->current_channel_id;
}

void dvb_recorder_stop(DVBRecorder *recorder)
{
    FLOG("\n");
    g_return_if_fail(recorder != NULL);

    if (recorder->record_status == DVB_RECORD_STATUS_RECORDING)
        dvb_recorder_record_stop(recorder);

    dvb_reader_stop(recorder->reader);
}

void dvb_recorder_record_callback(const guint8 *data, gsize size, DVBRecorder *recorder)
{
    FLOG("\n");
    ssize_t nw, offset;
    for (offset = 0; offset < size; offset += nw) {
        if ((nw = write(recorder->record_fd, data + offset, (size_t)(size - offset))) <= 0) {
            if (nw < 0) {
                LOG(&recorder->logger, "Could not write. Stop recording: %d (%s)\n", errno, strerror(errno));
                goto err;
            }
            else if (nw == 0) {
                LOG(&recorder->logger, "Written zero bytes. Trying again.\n");
                continue;
            }
            break;
        }
    }

    recorder->record_size += size;

    return;
err:
    dvb_recorder_record_stop(recorder);
}

void dvb_recorder_set_record_filename_pattern(DVBRecorder *recorder, const gchar *pattern)
{
    FLOG("\n");
    g_return_if_fail(recorder != NULL);

    g_free(recorder->record_filename_pattern);
    recorder->record_filename_pattern = g_strdup(pattern);
}

void dvb_recorder_set_snapshot_filename_pattern(DVBRecorder *recorder, const gchar *pattern)
{
    FLOG("\n");
}

void dvb_recorder_set_capture_dir(DVBRecorder *recorder, const gchar *capture_dir)
{
    FLOG("\n");
    g_return_if_fail(recorder != NULL);

    g_free(recorder->capture_dir);
    recorder->capture_dir = g_strdup(capture_dir);
}

struct _pattern_match_info {
    DVBStreamInfo *stream_info;
    struct tm *local_time;
};

static gboolean dvb_recorder_filename_pattern_eval(const GMatchInfo *matchinfo, GString *res, struct _pattern_match_info *info)
{
    FLOG("\n");
    gchar *match = g_match_info_fetch(matchinfo, 0);

    GRegex *fn_regex = g_regex_new("/", 0, 0, NULL);
    gchar *str = NULL;

    if (g_strcmp0(match, "${service_name}") == 0) {
        if (info->stream_info && info->stream_info->service_name) {
            str = g_regex_replace_literal(fn_regex, info->stream_info->service_name,
                    -1, 0, "_", 0, NULL);
            g_string_append(res, str);
        }
        fprintf(stderr, "matched service_name\n");
    }
    else if (g_strcmp0(match, "${service_provider}") == 0) {
        if (info->stream_info && info->stream_info->service_provider) {
            str = g_regex_replace_literal(fn_regex, info->stream_info->service_provider,
                    -1, 0, "_", 0, NULL);
            g_string_append(res, str);
        }
        fprintf(stderr, "matched service_provider\n");
    }
    else if (g_strcmp0(match, "${program_name}") == 0) {
        if (info->stream_info && info->stream_info->program_title) {
            str = g_regex_replace_literal(fn_regex, info->stream_info->program_title,
                    -1, 0, "_", 0, NULL);
            g_string_append(res, str);
        }
        fprintf(stderr, "matched program_name\n");
    }
    else if (g_str_has_prefix(match, "${date:")) {
        fprintf(stderr, "matched date\n");
        match[strlen(match) - 1] = 0;
        gchar tbuf[256];
        strftime(tbuf, 256, &match[7], info->local_time);
        g_string_append(res, tbuf);
    }
    else {
        fprintf(stderr, "unmatched match: %s\n", match);
    }

    g_free(str);
    g_regex_unref(fn_regex);
    g_free(match);

    return FALSE;
}

gchar *dvb_recorder_make_record_filename(DVBRecorder *recorder, const gchar *alternate_dir, const gchar *alternate_pattern)
{
    FLOG("\n");
    struct _pattern_match_info info;
    info.stream_info = dvb_reader_get_stream_info(recorder->reader);
    time_t t;
    t = time(NULL);
    info.local_time = localtime(&t);

        /* in extra function: allow placeholders in (user-defined) filename:
     * %{station_name}, %{station_provider}, %{date:%Y%m%d} */
    GRegex *regex = g_regex_new("\\${service_name}|\\${service_provider}|\\${program_name}|\\${date:[^}]*}",
                                0, 0, NULL);
    gchar *filename = g_regex_replace_eval(regex, alternate_pattern ? alternate_pattern : recorder->record_filename_pattern,
                                           -1, 0, 0,
                                           (GRegexEvalCallback)dvb_recorder_filename_pattern_eval, &info, NULL);

    dvb_stream_info_free(info.stream_info);

    g_regex_unref(regex);

    gchar *result = g_build_filename(
            alternate_dir ? alternate_dir : recorder->capture_dir,
            filename,
            NULL);

    g_free(filename);

    return result;
}

gboolean dvb_recorder_record_start(DVBRecorder *recorder)
{
    FLOG("\n");
    g_return_val_if_fail(recorder != NULL, FALSE);

    LOG(&recorder->logger, "dvb_recorder_record_start\n");

    if (recorder->record_status == DVB_RECORD_STATUS_RECORDING) {
        LOG(&recorder->logger, "Already recording\n");
        return FALSE;
    }

    /* Query reader status first and return error if not tuned in? */
    if (dvb_reader_get_stream_status(recorder->reader) != DVB_STREAM_STATUS_RUNNING) {
        LOG(&recorder->logger, "Stream not running.\n");
        return FALSE;
    }

    LOG(&recorder->logger, "make record filename\n");

    g_free(recorder->record_filename);
    recorder->record_filename = dvb_recorder_make_record_filename(recorder, NULL, NULL);

    /* g_path_get_dirname(), g_mkdir_with_parents */

    if (!recorder->record_filename) {
        LOG(&recorder->logger, "Could not generate filename\n");
        return FALSE;
    }

    LOG(&recorder->logger, "open record fd\n");

    recorder->record_fd = open(recorder->record_filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (recorder->record_fd == -1) {
        LOG(&recorder->logger, "Failed to open %s: (%d) %s\n", recorder->record_filename, errno, strerror(errno));
        return FALSE;
    }

    recorder->record_size = 0;
    time(&recorder->record_start);
    recorder->record_status = DVB_RECORD_STATUS_RECORDING;

    LOG(&recorder->logger, "set listener to record callback\n");
    dvb_reader_set_listener(recorder->reader, recorder->record_filter, -1,
            (DVBReaderListenerCallback)dvb_recorder_record_callback, recorder);
    dvb_reader_listener_set_running(recorder->reader, -1, (DVBReaderListenerCallback)dvb_recorder_record_callback, TRUE);

    LOG(&recorder->logger, "send event about status change\n");
    dvb_recorder_event_send(DVB_RECORDER_EVENT_RECORD_STATUS_CHANGED,
            recorder->event_cb, recorder->event_data,
            "status", DVB_RECORD_STATUS_RECORDING,
            NULL, NULL);

    LOG(&recorder->logger, "finished sending event\n");

    return TRUE;
}

void dvb_recorder_record_stop(DVBRecorder *recorder)
{
    FLOG("\n");
    g_return_if_fail(recorder != NULL);

    dvb_reader_remove_listener(recorder->reader, -1, (DVBReaderListenerCallback)dvb_recorder_record_callback);
    LOG(&recorder->logger, "dvb_recorder_record_stop, record_fd: %d\n", recorder->record_fd);
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
    FLOG("\n");
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

GList *dvb_recorder_get_epg(DVBRecorder *recorder)
{
    FLOG("\n");
    g_return_val_if_fail(recorder != NULL, NULL);

    return dvb_reader_get_events(recorder->reader);
}

EPGEvent *dvb_recorder_get_epg_event(DVBRecorder *recorder, guint16 event_id)
{
    FLOG("\n");
    g_return_val_if_fail(recorder != NULL, NULL);

    return dvb_reader_get_event(recorder->reader, event_id);
}

DVBStreamInfo *dvb_recorder_get_stream_info(DVBRecorder *recorder)
{
    FLOG("\n");
    g_return_val_if_fail(recorder != NULL, NULL);

    return dvb_reader_get_stream_info(recorder->reader);
}

DVBStreamStatus dvb_recorder_get_stream_status(DVBRecorder *recorder)
{
    FLOG("\n");
    g_return_val_if_fail(recorder != NULL, DVB_STREAM_STATUS_UNKNOWN);

    return dvb_reader_get_stream_status(recorder->reader);
}

void dvb_recorder_set_record_filter(DVBRecorder *recorder, DVBFilterType filter)
{
    FLOG("\n");
    g_return_if_fail(recorder != NULL);

    recorder->record_filter = filter;
}

DVBFilterType dvb_recorder_get_record_filter(DVBRecorder *recorder)
{
    FLOG("\n");
    g_return_val_if_fail(recorder != NULL, 0);

    return recorder->record_filter;
}

float dvb_recorder_get_signal_strength(DVBRecorder *recorder)
{
    if (recorder)
        return dvb_reader_query_signal_strength(recorder->reader);
    return -1.0f;
}


