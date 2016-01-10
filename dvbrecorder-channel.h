#pragma once

#include <glib.h>

typedef struct {
    guint64 channel_id;
    /* frequency, symbol_rate, polarization, … */
} DVBRecorderChannel;
