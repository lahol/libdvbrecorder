#pragma once

#include <glib.h>

typedef struct {
    gchar *service_provider;
    gchar *service_name;
    guint8 service_type;
    gchar *program_title;
} DVBStreamInfo;

void dvb_stream_info_free(DVBStreamInfo *info);
