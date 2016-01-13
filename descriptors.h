#pragma once

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    guint8 tag;
} dvb_si_descriptor;

/* 0x48: service_descriptor */
typedef struct {
    dvb_si_descriptor parent;

    guint8 type;
    gchar *provider;
    gchar *name;
} dvb_si_descriptor_service;

dvb_si_descriptor *dvb_si_descriptor_decode(dvbpsi_descriptor_t *desc);
void dvb_si_descriptor_free(dvb_si_descriptor *desc);
