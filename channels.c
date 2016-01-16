#include "channels.h"
#include <memory.h>
#include <stdlib.h>

void channel_data_copy(ChannelData *dst, ChannelData *src)
{
    if (dst == NULL || src == NULL)
        return;

    (*dst) = (*src);
    dst->name = g_strdup(src->name);
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
        g_free(data->name);
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
    data->name = g_strdup(tokens[0]);
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
    if (data == NULL || data->name == 0)
        return NULL;
    gchar **tokens = g_strsplit(data->name, ";", 2);
    gchar *result = NULL;
    if (g_strv_length(tokens) == 2)
        result = g_strdup_printf("%s(%s)", tokens[0], tokens[1]);
    else
        result = g_strdup(tokens[0]);

    g_strfreev(tokens);

    return result;
}
