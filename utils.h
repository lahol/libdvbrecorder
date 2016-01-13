#pragma once

#include <glib.h>
#include <stdint.h>
#include <time.h>

gboolean util_convert_string_control_codes(gchar **buf);
gchar *util_convert_string(uint8_t *str, uint8_t length);
uint32_t util_convert_bcd_time(uint32_t bcd, uint8_t *hours, uint8_t *minutes, uint8_t *seconds);
time_t util_convert_datetime(uint64_t datetime, struct tm **tm);
