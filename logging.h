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

/** @brief Log a formatted string to the logger, if present.
 *  @param[in] logger Handle to the logger.
 *  @param[in] format A format string to use.
 *  @param[in] ... The format parameters.
 */
void dvb_recorder_log(DVBRecorderLogger *logger, gchar *format, ...);
