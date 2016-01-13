#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <dvbpsi/descriptor.h>
#include "descriptors.h"
#include "utils.h"

/* 0x48: service_descriptor */
dvb_si_descriptor *dvb_si_descriptor_decode_service(dvbpsi_descriptor_t *desc)
{
    int o, l;
    dvb_si_descriptor_service *d = g_malloc0(sizeof(dvb_si_descriptor_service));
    ((dvb_si_descriptor *)d)->tag = 0x48;
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

dvb_si_descriptor *dvb_si_descriptor_decode(dvbpsi_descriptor_t *desc)
{
    switch (desc->i_tag) {
        case 0x48: /* service_descriptor */
            return dvb_si_descriptor_decode_service(desc);
        default:
            return NULL;
    }
}

void dvb_si_descriptor_free(dvb_si_descriptor *desc)
{
    if (desc == NULL)
        return;
    switch (desc->tag) {
        case 0x48:
            dvb_si_descriptor_free_service(desc);
            break;
        default:
            g_free(desc);
    }
}
