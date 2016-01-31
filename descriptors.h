#pragma once

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    dvb_si_tag_service_descriptor = 0x48,
    dvb_si_tag_short_event_descriptor = 0x4d,
    dvb_si_tag_extended_event_descriptor = 0x4e
} dvb_si_descriptor_tag;

typedef struct {
    dvb_si_descriptor_tag tag;
} dvb_si_descriptor;

/* 0x48: service_descriptor */
typedef struct {
    dvb_si_descriptor parent;

    guint8 type;
    gchar *provider;
    gchar *name;
} dvb_si_descriptor_service;

/* 0x4d short_event_descriptor */
typedef struct {
    dvb_si_descriptor parent;

    gchar language[4];
    gchar *description;
    gchar *text;
} dvb_si_descriptor_short_event;

/* 0x4e extended_event_descriptor */
typedef struct {
    gchar *description;
    gchar *content;
} dvb_si_descriptor_extended_event_item;

typedef struct {
    dvb_si_descriptor parent;

    guint8 descriptor_number;
    guint8 descriptor_last_number;
    gchar language[4];
    GList *items;
    gchar *text;
} dvb_si_descriptor_extended_event;

dvb_si_descriptor *dvb_si_descriptor_decode(dvbpsi_descriptor_t *desc);
void dvb_si_descriptor_free(dvb_si_descriptor *desc);

inline void dvb_si_descriptor_copy_iso639_lang(gchar *dst, gchar *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = '\0';
}
