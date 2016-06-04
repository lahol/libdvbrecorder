#pragma once

#include <glib.h>
#include "events.h"
#include "epg.h"
#include "streaminfo.h"
#include "filter.h"

typedef struct _DVBReader DVBReader;

DVBReader *dvb_reader_new(DVBRecorderEventCallback cb, gpointer userdata);
void dvb_reader_destroy(DVBReader *reader);

/* packet [188], type, userdata */
typedef void (*DVBReaderListenerCallback)(const guint8 *, DVBFilterType, gpointer);

void dvb_reader_set_listener(DVBReader *reader, DVBFilterType filter, int fd,
                             DVBReaderListenerCallback callback, gpointer userdata);
void dvb_reader_remove_listener(DVBReader *reader, int fd, DVBReaderListenerCallback callback);

gboolean dvb_reader_get_current_pat_packets(DVBReader *reader, guint8 **buffer, gsize *length);
gboolean dvb_reader_get_current_pmt_packets(DVBReader *reader, guint8 **buffer, gsize *length);

DVBStreamInfo *dvb_reader_get_stream_info(DVBReader *reader);

DVBStreamStatus dvb_reader_get_stream_status(DVBReader *reader);

void dvb_reader_tune(DVBReader *reader,
                     guint32 frequency,
                     guint8  polarization,
                     guint8  sat_no,
                     guint32 symbol_rate,
                     guint16 program_number);

void dvb_reader_start(DVBReader *reader);
void dvb_reader_stop(DVBReader *reader);

GList *dvb_reader_get_events(DVBReader *reader); /* new list containing reference of all events */
EPGEvent *dvb_reader_get_event(DVBReader *reader, guint16 eventid); /* [no transfer] */
