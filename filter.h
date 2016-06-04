#pragma once

typedef enum {
    DVB_FILTER_VIDEO     = (1 << 0),
    DVB_FILTER_AUDIO     = (1 << 1),
    DVB_FILTER_TELETEXT  = (1 << 2),
    DVB_FILTER_SUBTITLES = (1 << 3),
    DVB_FILTER_PAT       = (1 << 4),
    DVB_FILTER_PMT       = (1 << 5),
    DVB_FILTER_EIT       = (1 << 6),
    DVB_FILTER_SDT       = (1 << 7),
    DVB_FILTER_RST       = (1 << 8),
    DVB_FILTER_OTHER     = (1 << 9),
    DVB_FILTER_ALL       = 0x03ff
} DVBFilterType;
