#include "streaminfo.h"

void dvb_stream_info_free(DVBStreamInfo *info)
{
    if (info) {
        g_free(info->service_provider);
        g_free(info->service_name);
        g_free(info->program_title);
        g_free(info);
    }
}
