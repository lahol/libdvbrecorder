#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "logging.h"

void dvb_recorder_log(DVBRecorder *recorder, gchar *format, ...)
{
    DVBRecorderLoggerProc logger = NULL;
    gpointer userdata = NULL;
    if (!dvb_recorder_get_logger(recorder, &logger, &userdata))
        return;
    if (!logger)
        return;

    va_list args;
    gchar *buf = NULL;

    va_start(args, format);
    buf = g_strdup_vprintf(format, args);
    va_end(args);

    logger(buf, userdata);
    g_free(buf);
}

