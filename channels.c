#include "channels.h"
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

void channel_data_copy(ChannelData *dst, ChannelData *src)
{
    if (dst == NULL || src == NULL)
        return;

    (*dst) = (*src);
    dst->nameraw = g_strdup(src->nameraw);
    dst->name = g_strdup(src->name);
    dst->provider = g_strdup(src->provider);
    dst->parameter = g_strdup(src->parameter);
    dst->signalsource = g_strdup(src->signalsource);
    dst->vpid = g_strdup(src->vpid);
    dst->apid = g_strdup(src->apid);
}

void channel_data_update_payload(ChannelData *dst, ChannelData *src)
{
    if (dst == NULL || src == NULL)
        return;
    if (dst == src)
        return;
    guint32 dstid = dst->id;
    ChannelFlag flags = dst->flags;

    channel_data_clear(dst);
    channel_data_copy(dst, src);

    dst->id = dstid;
    dst->flags = flags;
}

ChannelData *channel_data_dup(ChannelData *data)
{
    if (data == NULL)
        return NULL;

    ChannelData *result = g_malloc(sizeof(ChannelData));
    channel_data_copy(result, data);

    return result;
}

void channel_data_clear(ChannelData *data)
{
    if (data) {
        g_free(data->nameraw);
        g_free(data->name);
        g_free(data->provider);
        g_free(data->parameter);
        g_free(data->signalsource);
        g_free(data->vpid);
        g_free(data->apid);
        memset(data, 0, sizeof(ChannelData));
    }
}

void channel_data_free(ChannelData *data) {
    channel_data_clear(data);
    g_free(data);
}

void channel_data_parse_vdr_parameter(ChannelData *channel)
{
    gchar *ptr = channel->parameter;
    if (ptr == NULL)
        return;
    while (*ptr != 0) {
        switch (*ptr) {
            case 'H':
            case 'h':
                channel->polarization = CHNL_POLARIZATION_HORIZONTAL;
                ++ptr;
                break;
            case 'V':
            case 'v':
                channel->polarization = CHNL_POLARIZATION_VERTICAL;
                ++ptr;
                break;
            case 'L':
            case 'l':
                channel->polarization = CHNL_POLARIZATION_LEFT;
                ++ptr;
                break;
            case 'R':
            case 'r':
                channel->polarization = CHNL_POLARIZATION_RIGHT;
                ++ptr;
                break;
            default:
                ++ptr;
                strtoul(ptr, &ptr, 10); /* read arguments */
                break;
        }
    }
}

ChannelData *channel_data_parse_vdr(gchar *line)
{
    /* http://www.vdr-wiki.de/wiki/index.php/Channels.conf */
    if (line == NULL)
        return NULL;
    ChannelData *data = NULL;
    gchar **tokens = g_strsplit(line, ":", 13);
    if (g_strv_length(tokens) != 13)
        goto out;

    data = g_malloc0(sizeof(ChannelData));
    data->nameraw = g_strdup(tokens[0]);
    channel_parse_name(data->nameraw, &data->name, &data->provider);
    data->frequency = (guint32)strtol(tokens[1], NULL, 10);
    data->parameter = g_strdup(tokens[2]);
    data->signalsource = g_strdup(tokens[3]);
    data->srate = (guint32)strtol(tokens[4], NULL, 10);
    data->vpid = g_strdup(tokens[5]);
    data->apid = g_strdup(tokens[6]);
    data->tpid = (guint32)strtol(tokens[7], NULL, 10);
    data->casid = (guint32)strtol(tokens[8], NULL, 10);
    data->sid = (guint32)strtol(tokens[9], NULL, 10);
    data->nid = (guint32)strtol(tokens[10], NULL, 10);
    data->tid = (guint32)strtol(tokens[11], NULL, 10);
    data->rid = (guint32)strtol(tokens[12], NULL, 10);

    channel_data_parse_vdr_parameter(data);

out:
    g_strfreev(tokens);
    return data;
}

/*ChannelData *channel_data_parse_xine(gchar *line, gchar *satellite)
{
    if (line == NULL)
        return NULL;
    ChannelData *data = NULL;
    gchar **tokens = g_strsplit(line, ":", 8);
    if (g_strv_length(tokens) != 8)
        goto out;

    data = g_malloc0(sizeof(ChannelData));
    data->name = g_strdup(tokens[0]);
    data->freq = (guint32)strtol(tokens[1], NULL, 10);
    data->polarization = tokens[2][0];
    data->rotorpos = (guint32)strtol(tokens[3], NULL, 10);
    data->srate = (guint32)strtol(tokens[4], NULL, 10);
    data->vpid = (guint32)strtol(tokens[5], NULL, 10);
    data->apid = (guint32)strtol(tokens[6], NULL, 10);
    data->serviceid = (guint32)strtol(tokens[7], NULL, 10);

out:
    g_strfreev(tokens);
    return data;
}
*/
ChannelData *channel_data_parse(gchar *line, const gchar *satellite)
{
    return channel_data_parse_vdr(line);
}

gchar *channel_convert_name_to_xine(ChannelData *data)
{
    if (data == NULL)
        return NULL;
    if (data->name && data->provider)
        return g_strdup_printf("%s(%s)", data->name, data->provider);
    else
        return g_strdup(data->nameraw);
}

void channel_parse_name(gchar *vdrraw, gchar **name, gchar **provider)
{
    if (vdrraw == NULL || vdrraw[0] == 0)
        return;
    gchar **tokens = g_strsplit(vdrraw, ";", 2);
    
    if (g_strv_length(tokens) == 2) {
        if (name) *name = g_strdup(tokens[0]);
        if (provider) *provider = g_strdup(tokens[1]);
    }
    else {
        if (name) *name = g_strdup(tokens[0]);
    }

    g_strfreev(tokens);
}

static const gchar *signalsource_translations[] = {
    "S19E2", "S19.2E",
    "S23E5", "S23.E5",
    "S13E0", "S13E",
    "S28E2", "S28.2E",
    "S9E0", "S9.0E",
    NULL, NULL
};

const gchar *channel_data_signalsource_key_to_string(const gchar *key)
{
    guint i;
    for (i = 0; signalsource_translations[i] != NULL; i += 2) {
        if (g_strcmp0(key, signalsource_translations[i]) == 0)
            return signalsource_translations[i+1];
    }

    return NULL;
}

const gchar *channel_data_signalsource_string_to_key(const gchar *string)
{
    guint i;
    for (i = 0; signalsource_translations[i] != NULL; i += 2) {
        if (g_strcmp0(key, signalsource_translations[i+1]) == 0)
            return signalsource_translations[i];
    }

    return NULL;
}
