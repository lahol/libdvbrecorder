#include "epg.h"
#include <stdint.h>
#include <stdbool.h>
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/eit.h>
#include <epg-internal.h>
#include "utils.h"
#include "descriptors.h"

void epg_event_read_descriptors(EPGEvent *event, dvbpsi_descriptor_t *desc)
{
    /* read descriptors via generic functions, concat extended_event,
     * make list of events in appropriate position */
    dvb_si_descriptor *d;
    for ( ; desc; desc = desc->p_next) {
        d = dvb_si_descriptor_decode(desc);
        switch (d->tag) {
            case dvb_si_tag_short_event_descriptor:
                {
                    dvb_si_descriptor_short_event *se_d = (dvb_si_descriptor_short_event *)d;
                    EPGShortEvent *se = g_malloc0(sizeof(EPGShortEvent));
                    dvb_si_descriptor_copy_iso639_lang(se->language, se_d->language);
                    /* steal pointers */
                    se->description = se_d->description;
                    se->text = se_d->text;
                    se_d->description = NULL;
                    se_d->text = NULL;

                    event->short_descriptions = g_list_prepend(event->short_descriptions, se);
                }
                break;
            case dvb_si_tag_extended_event_descriptor:
                {
                    dvb_si_descriptor_extended_event *ee_d = (dvb_si_descriptor_extended_event *)d;
                    EPGExtendedEvent *ee = g_malloc0(sizeof(EPGExtendedEvent));
                    /* collect descriptors */
                    /* ee “global”, GString for collecting text
                     * if number == 0 -> new GString
                     * if number == last_number -> write to list 
                     * (check first if all are read and in the right order?) */
                }
                break;
            default:
                break;
        }
        dvb_si_descriptor_free(d);
    }
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
        event->starttime = util_convert_datetime(tmp->i_start_time, NULL);
        event->duration = util_convert_bcd_time(tmp->i_duration, NULL, NULL, NULL);
    }
}
