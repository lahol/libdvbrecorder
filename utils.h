#pragma once

#include <glib.h>
#include <stdint.h>
#include <time.h>

gboolean util_convert_string_control_codes(gchar **buf);
gchar *util_convert_string(guint8 *str, guint8 length);
guint32 util_convert_bcd_time(guint32 bcd, guint8 *hours, guint8 *minutes, guint8 *seconds);
time_t util_convert_datetime(guint64 datetime, struct tm **tm);

typedef gpointer (*UtilDataDupFunc)(gpointer data);
GList *util_dup_list_deep(GList *list, UtilDataDupFunc datadup);
