#pragma once

#include <glib.h>
#include "channels.h"

typedef struct {
    guint32 id;
    gchar *title;
} ChannelDBList;

void channel_db_list_copy(ChannelDBList *dst, ChannelDBList *src);
void channel_db_list_clear(ChannelDBList *entry);
void channel_db_list_free(ChannelDBList *entry);

typedef void (*CHANNEL_DB_FOREACH_CALLBACK)(ChannelData *, gpointer);
typedef void (*CHANNEL_DB_LIST_FOREACH_CALLBACK)(ChannelDBList *, gpointer);

gint channel_db_init(const gchar *db_path);
void channel_db_dispose(void);
void channel_db_foreach(guint32 list_id, CHANNEL_DB_FOREACH_CALLBACK callback, gpointer userdata);
void channel_db_list_foreach(CHANNEL_DB_LIST_FOREACH_CALLBACK callback, gpointer userdata);
guint32 channel_db_set_channel(ChannelData *data);
ChannelData *channel_db_get_channel(guint32 id);

guint32 channel_db_list_add(const gchar *title);

void channel_db_list_add_entry(ChannelDBList *list, ChannelData *entry, gint pos);
void channel_db_list_update_entry(ChannelDBList *list, ChannelData *entry, gint pos);
void channel_db_list_remove_entry(ChannelDBList *list, ChannelData *entry);

void channel_db_start_transaction(void);
void channel_db_commit_transaction(void);
