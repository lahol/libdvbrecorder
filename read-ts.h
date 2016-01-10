#pragma once

#include <stdint.h>
#include <bitstream/mpeg/ts.h>

typedef struct _TsReader TsReader;

typedef struct _TsReaderClass {
    /* callbacks for packets/tables/… */
    /* Handle a packet. */
    gboolean (*handle_packet)(const uint8_t *, void *);
} TsReaderClass;

TsReader *ts_reader_new(TsReaderClass *klass, void *userdata);
void ts_reader_free(TsReader *reader);

void ts_reader_push_buffer(TsReader *reader, const uint8_t *buffer, size_t len);
