#include <string.h>
#include "utils.h"

gboolean util_convert_string_control_codes(gchar **buf)
{
    if (buf == NULL)
        return TRUE;
    gchar *p = *buf;
    gsize control_characters_count = 0;
    while (*p) {
        if ((*p & 0x80) == 0) { /* valid one byte character */
            p += 1;
        }
        else if ((*p & 0xE0) == 0xC0) { /* start of two byte character */
            if ((*(p + 1) & 0xC0) != 0x80) /* malformed UTF-8, check also ensures that *(p + 1) != 0 */
                return FALSE;
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0) { /* start of three byte character */
            if ((*(p + 1) & 0xC0) != 0x80 ||
                (*(p + 2) & 0xC0) != 0x80) /* malformed UTF-8 */
                return FALSE;
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0) { /* start of four byte character */
            if ((*(p + 1) & 0xC0) != 0x80 ||
                (*(p + 2) & 0xC0) != 0x80 ||
                (*(p + 3) & 0xC0) != 0x80) /* malformed UTF-8 */
                return FALSE;
            p += 4;
        }
        else if ((*p <= 0x9F) && *p >= 0x80) { /* control character not handled by conversion */
            ++control_characters_count;
            p += 1;
        }
        else
            return FALSE;
        /* two-byte control characters are already handled */
    }

    if (control_characters_count == 0) /* no control characters to convert, just return */
        return TRUE;

    gchar *buffer = g_malloc(strlen(*buf) + 1 + control_characters_count);
    gchar *q = buffer;
    p = *buf;
    do {
        if ((*p & 0x80) == 0x00) {
            *q = *p; ++p; ++q;    
        }
        else if ((*p & 0xE0) == 0xC0) {
            *q = *p; ++p; ++q;
            *q = *p; ++p; ++q;
        }
        else if ((*p & 0xF0) == 0xE0) {
            *q = *p; ++p; ++q;
            *q = *p; ++p; ++q;
            *q = *p; ++p; ++q;
        }
        else if ((*p & 0xF8) == 0xF0) {
            *q = *p; ++p; ++q;
            *q = *p; ++p; ++q;
            *q = *p; ++p; ++q;
            *q = *p; ++p; ++q;
        }
        else {
            *q = 0xC2; ++q;
            *q = *p; ++p; ++q;
        }
    } while (*p);

    g_free(*buf);
    *buf = buffer;

    return TRUE;
}

gchar *util_convert_string(uint8_t *str, uint8_t length)
{
    if (length == 0)
        return NULL;

    /* FIXME: clean up this mess, support the other codes, too */
    char *from;
    switch (str[0]) {
        case 0x05: 
            from = "ISO_8859-9";
            break;
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        default:
            from = "LATIN1";
    }

    if (str[0] <= 0x1f)
        ++str;

    gsize bytes_written;
    gchar *buf = g_convert((gchar *)str, length, "UTF-8", from, NULL, &bytes_written, NULL);
    util_convert_string_control_codes(&buf);

    return buf;
}

uint32_t util_convert_bcd_time(uint32_t bcd, uint8_t *hours, uint8_t *minutes, uint8_t *seconds)
{
    uint8_t h, m, s;
    h = 10 * ((bcd >> 20) & 0x0f) + ((bcd >> 16) & 0x0f);
    m = 10 * ((bcd >> 12) & 0x0f) + ((bcd >> 8) & 0x0f);
    s = 10 * ((bcd >> 4) & 0x0f) + (bcd & 0x0f);

    if (hours)
        *hours = h;
    if (minutes)
        *minutes = m;
    if (seconds)
        *seconds = s;

    return 3600 * h + 60 * m + s;
}

time_t util_convert_datetime(uint64_t datetime, struct tm **tm)
{
    time_t tval = ((datetime >> 24) - 40587) * 86400
                 + util_convert_bcd_time(datetime & 0xffffff, NULL, NULL, NULL);
    if (tm) {
        *tm = localtime(&tval);       
    }

    return tval;
}

