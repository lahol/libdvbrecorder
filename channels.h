#pragma once

#include <glib.h>

typedef enum {
    CHNL_FLAG_DIRTY  = 1 << 0
} ChannelFlag;

typedef enum {
    CHNL_POLARIZATION_HORIZONTAL,
    CHNL_POLARIZATION_VERTICAL,
    CHNL_POLARIZATION_LEFT,
    CHNL_POLARIZATION_RIGHT
} ChannelPolarization;

typedef struct {
    guint32 id;
    gchar *nameraw;        /* Channel name as we read it */
    gchar *name;           /* the channel name part of nameraw */
    gchar *provider;       /* the provider part of nameraw */
    guint32 frequency;     /* Frequency MHz for DVB-S*/
    gchar *parameter;      /* Parameter @see parse_vdr_param */
    ChannelPolarization polarization; /* Polarization */
    gchar *signalsource;   /* Source, e.g. S19E2 */
    guint32 srate;         /* Symbolrate */
    gchar *vpid;           /* Video-PID */
    gchar *apid;           /* Audio-PID */
    guint32 tpid;          /* teletext pid */
    guint32 casid;         /* conditional access id */
    guint32 sid;           /* service id */
    guint32 nid;           /* network id */
    guint32 tid;           /* transport stream id */
    guint32 rid;           /* radio id */
    ChannelFlag flags;
} ChannelData;

void channel_data_copy(ChannelData *dst, ChannelData *src);
void channel_data_update_payload(ChannelData *dst, ChannelData *src);
ChannelData *channel_data_dup(ChannelData *data);
void channel_data_clear(ChannelData *data);
void channel_data_free(ChannelData *data);

ChannelData *channel_data_parse(gchar *line, const gchar *satellite);
gchar *channel_convert_name_to_xine(ChannelData *data);
void channel_parse_name(gchar *vdrraw, gchar **name, gchar **provider);
void channel_data_parse_vdr_parameter(ChannelData *channel);

