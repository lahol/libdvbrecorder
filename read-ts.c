#include <glib.h>
#include <memory.h>

#include "read-ts.h"

struct _TsReader {
    TsReaderClass klass;
    void *cb_userdata;
    TS_DECLARE(packet_data);
    size_t bytes_read;

    uint8_t *buffer;
    size_t remaining;

    guint32 error_occured : 1;
};

gboolean ts_reader_handle_packet_fallback(const uint8_t *packet, void *userdata)
{
    return TRUE;
}

TsReaderClass ts_reader_class_fallback = {
    .handle_packet = ts_reader_handle_packet_fallback,
};

static inline void ts_reader_advance_buffer(TsReader *reader, size_t len)
{
    reader->buffer += len;
    reader->remaining -= len;
}

void ts_reader_sync_stream(TsReader *reader)
{
    /* return start of first valid sync byte */
    size_t offset = 0;
    int no_match = 0;
    size_t i;
    while (offset < reader->remaining) {
        if (ts_validate(reader->buffer)) { /* possible start */
            no_match = 0;
            /* check if we have the start of a packet (next five packets) */
            for (i = 1; i < 5 && no_match == 0; ++i) {
                if (offset + i * TS_SIZE < reader->remaining) {
                    if (!ts_validate(&reader->buffer[offset + i * TS_SIZE]))
                        no_match = 1;
                }
                else {
                    goto err;
                }
            }
            if (!no_match) {
                ts_reader_advance_buffer(reader, offset);
                return;
            }
        }
        ++offset;
    }
err:
    /* drop buffer if not enough bytes in buffer to sync */
    reader->remaining = 0;
}

static inline void ts_reader_process_packet(TsReader *reader)
{
    if (ts_validate(reader->packet_data)) {
        if (!reader->klass.handle_packet(reader->packet_data, reader->cb_userdata))
            reader->error_occured = 1;
    }
    else {
        reader->bytes_read = 0;
        ts_reader_sync_stream(reader);
    }
}

static inline void ts_reader_read_packet_partial(TsReader *reader)
{
    if (reader->remaining < TS_SIZE - reader->bytes_read) {
        memcpy(&reader->packet_data[reader->bytes_read], reader->buffer, reader->remaining);
        reader->bytes_read += reader->remaining;
        ts_reader_advance_buffer(reader, reader->remaining);
        return;
    }
    memcpy(&reader->packet_data[reader->bytes_read], reader->buffer, TS_SIZE - reader->bytes_read);
    ts_reader_advance_buffer(reader, TS_SIZE - reader->bytes_read);

    reader->bytes_read = 0;
    ts_reader_process_packet(reader);
}

TsReader *ts_reader_new(TsReaderClass *klass, void *userdata)
{
    TsReader *reader = g_malloc0(sizeof(TsReader));
    if (!reader)
        return NULL;
    if (klass)
        reader->klass = *klass;
    else
        reader->klass = ts_reader_class_fallback;

    if (!reader->klass.handle_packet)
        reader->klass.handle_packet = ts_reader_handle_packet_fallback;

    reader->cb_userdata = userdata;

    return reader;
}

void ts_reader_free(TsReader *reader)
{
    g_free(reader);
}

void ts_reader_push_buffer(TsReader *reader, const uint8_t *buffer, size_t len)
{
    /* if bytes_read < 188 read min{188-bytes_read,len} bytes from buffer
     * else check if byte 0 valid; if not sync stream */
    reader->buffer = (uint8_t *)buffer;
    reader->remaining = len;

    if (reader->bytes_read == 0 && !ts_validate(reader->buffer))
        ts_reader_sync_stream(reader);

    while (reader->remaining && !reader->error_occured) {
        ts_reader_read_packet_partial(reader);
    }
}
