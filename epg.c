#include "epg.h"
#include <stdint.h>
#include <stdbool.h>
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/eit.h>
#include <epg-internal.h>
#include <stdio.h>
#include "utils.h"
#include "descriptors.h"

void epg_extended_event_item_free(EPGExtendedEventItem *item)
{
    if (item) {
        g_free(item->description);
        g_free(item->content);
        g_free(item);
    }
}

void epg_extended_event_free(EPGExtendedEvent *event)
{
    if (event) {
        g_list_free_full(event->description_items, (GDestroyNotify)epg_extended_event_item_free);
        g_free(event->text);
        g_free(event);
    }
}

void epg_short_event_free(EPGShortEvent *event)
{
    if (event) {
        g_free(event->description);
        g_free(event->text);
        g_free(event);
    }
}

void epg_event_read_descriptors(EPGEvent *event, dvbpsi_descriptor_t *desc)
{
    /* read descriptors via generic functions, concat extended_event,
     * make list of events in appropriate position */
    dvb_si_descriptor *d;
    dvb_si_descriptor_short_event *se_d = NULL;
    dvb_si_descriptor_extended_event *ee_d = NULL;
    GString *collect_ee_text = NULL;
    GList *tmp;

    EPGShortEvent *se = NULL;
    EPGExtendedEvent *ee = NULL;
    EPGExtendedEventItem *item;

    for ( ; desc; desc = desc->p_next) {
        if ((d = dvb_si_descriptor_decode(desc)) == NULL)
            continue;
        switch (d->tag) {
            case dvb_si_tag_short_event_descriptor:
                {
                    se_d = (dvb_si_descriptor_short_event *)d;
                    se = g_malloc0(sizeof(EPGShortEvent));
                    dvb_si_descriptor_copy_iso639_lang(se->language, se_d->language);
                    /* steal pointers */
                    se->description = se_d->description;
                    se->text = se_d->text;
                    se_d->description = NULL;
                    se_d->text = NULL;

                    event->short_descriptions = g_list_prepend(event->short_descriptions, se);
                    se = NULL;
                }
                break;
            case dvb_si_tag_extended_event_descriptor:
                {
                    /* collect descriptors */
                    /* ee “global”, GString for collecting text
                     * if number == 0 -> new GString
                     * if number == last_number -> write to list 
                     * (check first if all are read and in the right order?) */

                    ee_d = (dvb_si_descriptor_extended_event *)d;
                    if (ee_d->descriptor_number == 0) {
                        if (G_UNLIKELY(collect_ee_text != NULL || ee != NULL)) {
                            fprintf(stderr, "Extended event descriptor discontinuity.\n");
                            g_string_free(collect_ee_text, TRUE);
                            epg_extended_event_free(ee);
                        }
                        ee = g_malloc0(sizeof(EPGExtendedEvent));
                        collect_ee_text = g_string_new(NULL);
                        dvb_si_descriptor_copy_iso639_lang(ee->language, ee_d->language);
                    }

                    if (G_UNLIKELY(collect_ee_text == NULL || ee == NULL)) {
                        fprintf(stderr, "Extended event descriptor not started.\n");
                        break;
                    }
                    else {
                        g_string_append(collect_ee_text, ee_d->text);
                        for (tmp = ee_d->items; tmp; tmp = g_list_next(tmp)) {
                            item = g_malloc(sizeof(EPGExtendedEventItem));

                            item->description = ((dvb_si_descriptor_extended_event_item *)tmp->data)->description;
                            item->content = ((dvb_si_descriptor_extended_event_item *)tmp->data)->content;
                            ((dvb_si_descriptor_extended_event_item *)tmp->data)->description = NULL;
                            ((dvb_si_descriptor_extended_event_item *)tmp->data)->content = NULL;

                            ee->description_items = g_list_prepend(ee->description_items, item);
                        }
                    }

                    if (ee_d->descriptor_number == ee_d->descriptor_last_number) {
                        ee->text = g_string_free(collect_ee_text, FALSE);
                        ee->description_items = g_list_reverse(ee->description_items);

                        event->extended_descriptions = g_list_prepend(event->extended_descriptions, ee);
                        ee = NULL;
                        collect_ee_text = NULL;
                    }
                }
                break;
            default:
                break;
        }
        dvb_si_descriptor_free(d);
    }
    
    event->short_descriptions = g_list_reverse(event->short_descriptions);
    event->extended_descriptions = g_list_reverse(event->extended_descriptions);
}

void _lib_dump_event(EPGEvent *event)
{
    size_t sz = sizeof(EPGEvent);
    gchar *ptr = (gchar *)event;
    gchar *end = ptr + sz;
    fprintf(stderr, "[lib] dump event %p [%zd]\n", event, sz);
    while (ptr < end) {
        fprintf(stderr, "%02x ", *ptr & 0xff);
        ++ptr;
    }
    fprintf(stderr, "\n");
}

GList *epg_read_table(dvbpsi_eit_t *eit)
{
    GList *event_list = NULL;
    guint8 table_id = eit->i_table_id;
    dvbpsi_eit_event_t *tmp;
    EPGEvent *event;
    for (tmp = eit->p_first_event; tmp; tmp = tmp->p_next) {
        event = g_malloc0(sizeof(EPGEvent));
        event->running_status = tmp->i_running_status;
        event->event_id = tmp->i_event_id;
        event->table_id = table_id;
        event->starttime = util_convert_datetime(tmp->i_start_time, NULL);
        event->duration = util_convert_bcd_time(tmp->i_duration, NULL, NULL, NULL);

        epg_event_read_descriptors(event, tmp->p_first_descriptor);
        fprintf(stderr, "epg_read_table: short: %p, ext: %p\n",
                event->short_descriptions, event->extended_descriptions);

        fprintf(stderr, "add event 0x%02x to table 0x%02x\n", event->event_id, event->table_id);
/*        _lib_dump_event(event);*/
        event_list = g_list_prepend(event_list, event);
    }

    return g_list_reverse(event_list);
}

void epg_event_free(EPGEvent *event)
{
    if (event) {
        g_list_free_full(event->short_descriptions, (GDestroyNotify)epg_short_event_free);
        g_list_free_full(event->extended_descriptions, (GDestroyNotify)epg_extended_event_free);
    }
}

gint epg_event_compare_time(EPGEvent *a, EPGEvent *b)
{
    if (a == NULL)
        return 1;
    if (b == NULL)
        return -1;
    if (a->starttime < b->starttime)
        return -1;
    else if (a->starttime > b->starttime)
        return 1;
    return 0;
}

EPGShortEvent *epg_short_event_dup(EPGShortEvent *event)
{
    if (event == NULL)
        return NULL;
    EPGShortEvent *dup = g_malloc(sizeof(EPGShortEvent));
    *dup = *event;
    dup->description = g_strdup(event->description);
    dup->text = g_strdup(event->text);

    return dup;
}

EPGExtendedEventItem *epg_extended_event_item_dup(EPGExtendedEventItem *item)
{
    if (item == NULL)
        return NULL;
    EPGExtendedEventItem *dup = g_malloc(sizeof(EPGExtendedEventItem));
    dup->description = g_strdup(item->description);
    dup->content = g_strdup(item->content);

    return dup;
}

EPGExtendedEvent *epg_extended_event_dup(EPGExtendedEvent *event)
{
    if (event == NULL)
        return NULL;
    EPGExtendedEvent *dup = g_malloc(sizeof(EPGExtendedEvent));
    *dup = *event;
    dup->description_items = util_dup_list_deep(event->description_items, (UtilDataDupFunc)epg_extended_event_item_dup);
    dup->text = g_strdup(event->text);

    return dup;
}

EPGEvent *epg_event_dup(EPGEvent *event)
{
    if (event == NULL)
        return NULL;
    EPGEvent *dup = g_malloc(sizeof(EPGEvent));
    *dup = *event;

    dup->short_descriptions = util_dup_list_deep(event->short_descriptions, (UtilDataDupFunc)epg_short_event_dup);
    dup->extended_descriptions = util_dup_list_deep(event->extended_descriptions, (UtilDataDupFunc)epg_extended_event_dup);

    return dup;
}
