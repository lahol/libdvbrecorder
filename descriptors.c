#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <dvbpsi/descriptor.h>
#include "descriptors.h"
#include "utils.h"
#include <stdio.h>

void dump_descriptor(dvbpsi_descriptor_t *desc)
{
    uint8_t i;
    for (i = 0; i < desc->i_length; ++i) {
        fprintf(stderr, "%02x ", desc->p_data[i] & 0xff);
        if (i % 16 == 15)
            fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

/* 0x48: service_descriptor */
dvb_si_descriptor *dvb_si_descriptor_decode_service(dvbpsi_descriptor_t *desc)
{
    int o, l;
    dvb_si_descriptor_service *d = g_malloc0(sizeof(dvb_si_descriptor_service));
    ((dvb_si_descriptor *)d)->tag = dvb_si_tag_service_descriptor;
    d->type = desc->p_data[0];
    o = 2;
    l = desc->p_data[1];
    d->provider = util_convert_string(&desc->p_data[o], l);
    o += l;
    l = desc->p_data[o];
    ++o;
    d->name = util_convert_string(&desc->p_data[o], l);

    return (dvb_si_descriptor *)d;
}

void dvb_si_descriptor_free_service(dvb_si_descriptor *desc)
{
    dvb_si_descriptor_service *d = (dvb_si_descriptor_service *)desc;

    g_free(d->provider);
    g_free(d->name);
    g_free(d);
}

/* 0x4d: short_event_descriptor */
dvb_si_descriptor *dvb_si_descriptor_decode_short_event(dvbpsi_descriptor_t *desc)
{
    int o, l;
    dvb_si_descriptor_short_event *d = g_malloc0(sizeof(dvb_si_descriptor_short_event));
    ((dvb_si_descriptor *)d)->tag = dvb_si_tag_short_event_descriptor;

    dvb_si_descriptor_copy_iso639_lang(d->language, (gchar *)&desc->p_data[0]);
    o = 4;
    l = desc->p_data[3];
    d->description = util_convert_string(&desc->p_data[o], l);
    o += l;
    l = desc->p_data[o];
    ++o;
    d->text = util_convert_string(&desc->p_data[o], l);

/*    dump_descriptor(desc);
    fprintf(stderr, "decode_sort_event: [%s] “%s”, “%s”\n", d->language, d->description, d->text);*/

    return (dvb_si_descriptor *)d;
}

void dvb_si_descriptor_free_short_event(dvb_si_descriptor *desc)
{
    dvb_si_descriptor_short_event *d = (dvb_si_descriptor_short_event *)desc;

    g_free(d->description);
    g_free(d->text);
    g_free(d);
}

/* 0x4e: extended_event_descriptor */
dvb_si_descriptor *dvb_si_descriptor_decode_extended_event(dvbpsi_descriptor_t *desc)
{
    int l;
    dvb_si_descriptor_extended_event *d = g_malloc0(sizeof(dvb_si_descriptor_extended_event));
    ((dvb_si_descriptor *)d)->tag = dvb_si_tag_extended_event_descriptor;

    d->descriptor_number = desc->p_data[0] >> 4;
    d->descriptor_last_number = desc->p_data[0] & 0x0f;

    dvb_si_descriptor_copy_iso639_lang(d->language, (gchar *)&desc->p_data[1]);
    guint8 i_len = desc->p_data[4];
    guint8 *p;

    dvb_si_descriptor_extended_event_item *item;

    for (p = &desc->p_data[5]; p < &desc->p_data[5 + i_len]; ) {
        item = g_malloc0(sizeof(dvb_si_descriptor_extended_event_item));
        l = p[0];
        item->description = util_convert_string(&p[1], l);
        p += l + 1;

        l = p[0];
        item->content = util_convert_string(&p[1], l);
        p += l + 1;

        d->items = g_list_prepend(d->items, item);
    }

    d->items = g_list_reverse(d->items);

    l = desc->p_data[5 + i_len];
    d->text = util_convert_string(&desc->p_data[6 + i_len], l);

    return (dvb_si_descriptor *)d;
}

void dvb_si_descriptor_free_extended_event_item(dvb_si_descriptor_extended_event_item *item)
{
    g_free(item->description);
    g_free(item->content);
    g_free(item);
}

void dvb_si_descriptor_free_extended_event(dvb_si_descriptor *desc)
{
    dvb_si_descriptor_extended_event *d = (dvb_si_descriptor_extended_event *)desc;
    g_free(d->text);
    g_list_free_full(d->items, (GDestroyNotify)dvb_si_descriptor_free_extended_event_item);
    g_free(d);
}

dvb_si_descriptor *dvb_si_descriptor_decode(dvbpsi_descriptor_t *desc)
{
#define CASE(t) case dvb_si_tag_ ## t ## _descriptor: return dvb_si_descriptor_decode_ ## t ( desc )

    switch (desc->i_tag) {
        CASE(service);
        CASE(short_event);
        CASE(extended_event);
        default:
            return NULL;
    }
#undef CASE
}

void dvb_si_descriptor_free(dvb_si_descriptor *desc)
{
    if (desc == NULL)
        return;
#define CASE(t) case dvb_si_tag_ ## t ## _descriptor: dvb_si_descriptor_free_ ## t ( desc ); break
    switch (desc->tag) {
        CASE(service);
        CASE(short_event);
        CASE(extended_event);
        default:
            g_free(desc);
    }
#undef CASE
}
