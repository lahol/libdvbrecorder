#pragma once

#include <glib.h>
#include "dvbrecorder-event.h"

typedef struct _DVBReader DVBReader;

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
    DVB_FILTER_UNKNOWN   = (1 << 9)
} DVBReaderFilterType;

DVBReader *dvb_reader_new(DVBRecorderEventCallback cb, gpointer userdata);
void dvb_reader_destroy(DVBReader *reader);

void dvb_reader_set_listener(DVBReader *reader, int fd, DVBReaderFilterType filter);
void dvb_reader_remove_listener(DVBReader *reader, int fd);

void dvb_reader_tune(DVBReader *reader,
                     guint32 frequency,
                     guint8  polarization,
                     guint8  sat_no,
                     guint32 symbol_rate,
                     guint16 program_number);

void dvb_reader_start(DVBReader *reader);
void dvb_reader_stop(DVBReader *reader);
