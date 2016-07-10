#pragma once

#include <glib.h>
#include "dvbrecorder.h"

void dvb_recorder_log(DVBRecorder *recorder, gchar *format, ...);

#ifdef DEBUG
#define DLOG(rec, fmt, ...) dvb_recorder_log(rec, "*DEBUG* " fmt, ##__VA_ARGS__)
#else
#define DLOG(rec, fmt, ...)
#endif

#ifdef FDEBUG
#define FLOG(fmt, ...) fprintf(stderr, "%s " fmt, __func__, ##__VA_ARGS__)
#else
#define FLOG(fmt, ...)
#endif

#define LOG(rec, fmt, ...) dvb_recorder_log(rec, fmt, ##__VA_ARGS__)


