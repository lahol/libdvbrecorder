#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "logging.h"

void dvb_recorder_log(DVBRecorderLogger *logger, gchar *format, ...)
{
    if (!logger || !logger->log)
        return;

    va_list args;
    gchar *buf = NULL;

    va_start(args, format);
    buf = g_strdup_vprintf(format, args);
    va_end(args);

    logger->log(buf, logger->data);
    g_free(buf);
}

