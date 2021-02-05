#pragma once

#include <glib.h>

/** @brief Callback type for logging.
 *  @param[in] gchar* Format string.
 *  @param[in] gpointer Pointer to user data.
 */
typedef void (*DVBRecorderLoggerProc)(gchar *, gpointer);

/** @brief Logger handle.
 */
typedef struct _DVBRecorderLogger {
    DVBRecorderLoggerProc log;
    gpointer data;
} DVBRecorderLogger;

void dvb_recorder_log(DVBRecorderLogger *logger, gchar *format, ...);

#ifdef DEBUG
#define DLOG(logger, fmt, ...) dvb_recorder_log((logger), "[libdvbrecorder] *DEBUG* " fmt, ##__VA_ARGS__)
#else
#define DLOG(logger, fmt, ...)
#endif

#ifdef FDEBUG
#define FLOG(fmt, ...) fprintf(stderr, "%s " fmt, __func__, ##__VA_ARGS__)
#else
#define FLOG(fmt, ...)
#endif

#define LOG(logger, fmt, ...) dvb_recorder_log((logger), "[libdvbrecorder] " fmt, ##__VA_ARGS__)


