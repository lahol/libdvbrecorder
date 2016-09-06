#include "channel-db.h"
/*#include "config.h"*/
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduled.h"

sqlite3 *dbhandler_db = NULL;
sqlite3_stmt *fav_update_entry_stmt = NULL;
sqlite3_stmt *fav_insert_entry_stmt = NULL;
sqlite3_stmt *fav_delete_entry_stmt = NULL;
sqlite3_stmt *insert_channel_stmt = NULL;
sqlite3_stmt *update_channel_stmt = NULL;

void channel_db_list_copy(ChannelDBList *dst, ChannelDBList *src)
{
    if (dst == NULL)
        return;
    if (src == NULL) {
        memset(dst, 0, sizeof(ChannelDBList));
        return;
    }
    dst->id = src->id;
    dst->title = g_strdup(src->title);
}

void channel_db_list_clear(ChannelDBList *list)
{
    if (list) {
        g_free(list->title);

        memset(list, 0, sizeof(ChannelDBList));
    }
}

void channel_db_list_free(ChannelDBList *list)
{
    channel_db_list_clear(list);
    g_free(list);
}

gint channel_db_init(const gchar *db_path)
{
    int rc;
    char *sql;
    rc = sqlite3_open(db_path, &dbhandler_db);
    if (rc != 0)
        goto out;

    sql = "create table if not exists channels(chnl_id integer primary key, chnl_name varchar(255),\
        chnl_freq integer, chnl_parameter varchar(64), chnl_signalsource varchar(64), chnl_srate integer,\
        chnl_vpid varchar(128), chnl_apid varchar(128), chnl_tpid integer, chnl_casid integer, chnl_sid integer,\
        chnl_nid integer, chnl_tid integer, chnl_rid integer, chnl_flags integer)";
    rc = sqlite3_exec(dbhandler_db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK)
        goto out;

    sql = "create table if not exists favlists(id integer primary key, title varchar(255))";
    rc = sqlite3_exec(dbhandler_db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK)
        goto out;

    sql = "create table if not exists favourites(chnl_id integer, list_id integer, position integer)";
    rc = sqlite3_exec(dbhandler_db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK)
        goto out;

    if (scheduled_events_db_init() != 0)
        goto out;

    return 0;

out:
    channel_db_dispose();
    return 1;
}

void channel_db_dispose(void)
{
    if (fav_insert_entry_stmt) {
        sqlite3_finalize(fav_insert_entry_stmt);
        fav_insert_entry_stmt = NULL;
    }
    if (fav_update_entry_stmt) {
        sqlite3_finalize(fav_update_entry_stmt);
        fav_update_entry_stmt = NULL;
    }
    if (fav_delete_entry_stmt) {
        sqlite3_finalize(fav_delete_entry_stmt);
        fav_delete_entry_stmt = NULL;
    }
    if (insert_channel_stmt) {
        sqlite3_finalize(insert_channel_stmt);
        insert_channel_stmt = NULL;
    }
    if (update_channel_stmt) {
        sqlite3_finalize(update_channel_stmt);
        update_channel_stmt = NULL;
    }

    scheduled_events_db_cleanup();

    if (dbhandler_db) {
        sqlite3_close(dbhandler_db);
        dbhandler_db = NULL;
    }
}

void channel_db_foreach(guint32 list_id, CHANNEL_DB_FOREACH_CALLBACK callback, gpointer userdata)
{
    if (callback == NULL || dbhandler_db == NULL)
        return;

    char *sql = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    ChannelData data;

    if (list_id == 0) {
        sql = sqlite3_mprintf("select *\
                from channels order by chnl_id asc");
    }
    else {
        sql = sqlite3_mprintf("select channels.* from channels left outer join favourites on channels.chnl_id=favourites.chnl_id\
                where favourites.list_id=%" G_GUINT32_FORMAT " order by favourites.position asc", list_id);
    }

    rc = sqlite3_prepare_v2(dbhandler_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "error: %s\n", sqlite3_errmsg(dbhandler_db));
    }
    sqlite3_free(sql);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        data.id = sqlite3_column_int(stmt, 0);
        data.nameraw = (gchar *)sqlite3_column_text(stmt, 1);
        channel_parse_name(data.nameraw, &data.name, &data.provider);
        data.frequency = sqlite3_column_int(stmt, 2);
        data.parameter = (gchar *)sqlite3_column_text(stmt, 3);
        data.signalsource = (gchar *)sqlite3_column_text(stmt, 4);
        data.srate = sqlite3_column_int(stmt, 5);
        data.vpid = (gchar *)sqlite3_column_text(stmt, 6);
        data.apid = (gchar *)sqlite3_column_text(stmt, 7);
        data.tpid = sqlite3_column_int(stmt, 8);
        data.casid = sqlite3_column_int(stmt, 9);
        data.sid = sqlite3_column_int(stmt, 10);
        data.nid = sqlite3_column_int(stmt, 11);
        data.tid = sqlite3_column_int(stmt, 12);
        data.rid = sqlite3_column_int(stmt, 13);
        data.flags = sqlite3_column_int(stmt, 14);

        channel_data_parse_vdr_parameter(&data);

        callback(&data, userdata);
        g_free(data.name);
        g_free(data.provider);
    }

    if (stmt != NULL)
        sqlite3_finalize(stmt);
}

void channel_db_list_foreach(CHANNEL_DB_LIST_FOREACH_CALLBACK callback, gpointer userdata)
{
    if (callback == NULL || dbhandler_db == NULL)
        return;

    char *sql = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    ChannelDBList data;

    /* all channels, no favourites */
    data.id = 0;
    data.title = "All Channels";
    callback(&data, userdata);

    sql = "select id, title from favlists order by id asc";

    rc = sqlite3_prepare_v2(dbhandler_db, sql, -1, &stmt, NULL);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        data.id = sqlite3_column_int(stmt, 0);
        data.title = (gchar *)sqlite3_column_text(stmt, 1);

        callback(&data, userdata);
    }

    if (stmt != NULL)
        sqlite3_finalize(stmt);
}

guint32 channel_db_set_channel(ChannelData *data)
{
    g_return_val_if_fail(data != NULL, 0);

    sqlite3_stmt *stmt = NULL;
    int rc;

    if (data->id == 0) {
        if (insert_channel_stmt == NULL) {
            rc = sqlite3_prepare_v2(dbhandler_db,
                    "insert into channels (chnl_name, chnl_freq, chnl_parameter, chnl_signalsource,\
                    chnl_srate, chnl_vpid, chnl_apid, chnl_tpid, chnl_casid, chnl_sid, chnl_nid, chnl_tid, chnl_rid, chnl_flags)\
                    values (?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
                    -1, &insert_channel_stmt, NULL);
            if (rc != SQLITE_OK)
                return 0;
        }
        stmt = insert_channel_stmt;
    }
    else {
        if (update_channel_stmt == NULL) {
            rc = sqlite3_prepare_v2(dbhandler_db,
                    "update channels set chnl_name=?, chnl_freq=?, chnl_parameter=?, chnl_signalsource=?,\
                    chnl_srate=?, chnl_vpid=?, chnl_apid=?, chnl_tpid=?, chnl_casid=?, chnl_sid=?, chnl_nid=?,\
                    chnl_tid=?, chnl_rid=?, chnl_flags=? where chnl_id=?;",
                    -1, &update_channel_stmt, NULL);
            if (rc != SQLITE_OK)
                return 0;
        }
        sqlite3_bind_int(update_channel_stmt, 15, data->id);
        stmt = update_channel_stmt;
    }
    sqlite3_bind_text(stmt, 1, data->nameraw, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, data->frequency);
    sqlite3_bind_text(stmt, 3, data->parameter, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, data->signalsource, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, data->srate);
    sqlite3_bind_text(stmt, 6, data->vpid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, data->apid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, data->tpid);
    sqlite3_bind_int(stmt, 9, data->casid);
    sqlite3_bind_int(stmt, 10, data->sid);
    sqlite3_bind_int(stmt, 11, data->nid);
    sqlite3_bind_int(stmt, 12, data->tid);
    sqlite3_bind_int(stmt, 13, data->rid);
    sqlite3_bind_int(stmt, 14, data->flags);

    rc = sqlite3_step(stmt);
    sqlite3_reset(stmt);


    if (rc != SQLITE_OK && rc != SQLITE_DONE)
        return 0;

    if (data->id == 0)
        return (guint32)sqlite3_last_insert_rowid(dbhandler_db);
    return data->id;
}

ChannelData *channel_db_get_channel(guint32 id)
{
    char *sql = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    ChannelData *data = NULL;

    if (dbhandler_db == NULL)
        return NULL;

    sql = sqlite3_mprintf("select * from channels where chnl_id=%d", id);

    rc = sqlite3_prepare_v2(dbhandler_db, sql, -1, &stmt, NULL);
    sqlite3_free(sql);

    if (rc != SQLITE_OK)
        return NULL;

    if ((rc = sqlite3_step(stmt)) != SQLITE_ROW) {
        fprintf(stderr, "get_channel rc (step): %d\n", rc);
        goto done;
    }

    data = g_malloc0(sizeof(ChannelData));
    data->id = id;
    data->nameraw = g_strdup((gchar *)sqlite3_column_text(stmt, 1));
    channel_parse_name(data->nameraw, &data->name, &data->provider);
    data->frequency = sqlite3_column_int(stmt, 2);
    data->parameter = g_strdup((gchar *)sqlite3_column_text(stmt, 3));
    data->signalsource = g_strdup((gchar *)sqlite3_column_text(stmt, 4));
    data->srate = sqlite3_column_int(stmt, 5);
    data->vpid = g_strdup((gchar *)sqlite3_column_text(stmt, 6));
    data->apid = g_strdup((gchar *)sqlite3_column_text(stmt, 7));
    data->tpid = sqlite3_column_int(stmt, 8);
    data->casid = sqlite3_column_int(stmt, 9);
    data->sid = sqlite3_column_int(stmt, 10);
    data->nid = sqlite3_column_int(stmt, 11);
    data->tid = sqlite3_column_int(stmt, 12);
    data->rid = sqlite3_column_int(stmt, 13);
    data->flags = sqlite3_column_int(stmt, 14);

    channel_data_parse_vdr_parameter(data);

done:
    if (stmt != NULL)
        sqlite3_finalize(stmt);

    return data;

}

#if 0
void _channel_db_dump_channels_to_file_xine_cb(ChannelData *data, FILE *f)
{
    gchar *channel_name = channel_convert_name_to_xine(data);
    fprintf(f, "%s:", channel_name);
    g_free(channel_name);

    fprintf(f, "%u:", data->frequency);
    gchar pol = 'h';
    guint i;
    for (i = 0; data->parameter && data->parameter[i] != '\0'; ++i) {
        if (data->parameter[i] == 'h' || data->parameter[i] == 'v' ||
            data->parameter[i] == 'l' || data->parameter[i] == 'r') {
            pol = data->parameter[i];
            break;
        }
    }
    fprintf(f, "%c:0:", pol);
    fprintf(f, "%u:", data->srate);
    fprintf(f, "%lu:%lu:", strtoul(data->vpid, NULL, 10), strtoul(data->apid, NULL, 10));
    fprintf(f, "%u\n", data->sid);
}

void channel_db_dump_channels_to_file(void)
{
    gchar *file_format = NULL;
    gchar *filename = NULL;
    if (config_get("dvb", "scan-file-format", CFG_TYPE_STRING, &file_format) != 0)
        goto err;
    if (config_get("dvb", "scan-file", CFG_TYPE_STRING, &filename) != 0)
        goto err;
    FILE *f;
    if ((f = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "Could not open file %s\n", filename);
        goto err;
    }
    if (g_strcmp0(file_format, "xine") == 0) {
        channel_db_foreach(0, (CHANNEL_DB_FOREACH_CALLBACK)_channel_db_dump_channels_to_file_xine_cb, f);
    }
    else {
        fprintf(stderr, "Unsupported output format\n");
        goto err;
    }
err:
    if (f)
        fclose(f);
    g_free(file_format);
    g_free(filename);
}
#endif
guint32 channel_db_list_add(const gchar *title)
{
    if (dbhandler_db == NULL || title == NULL || title[0] == '\0')
        return 0;

    int rc;
    gchar *sql = sqlite3_mprintf("insert into favlists (title) values (%Q)", title);

    rc = sqlite3_exec(dbhandler_db, sql, NULL, NULL, NULL);

    sqlite3_free(sql);

    if (rc == SQLITE_OK) {
        return (guint32)sqlite3_last_insert_rowid(dbhandler_db);
    }
    else
        return 0;
}

void channel_db_list_add_entry(ChannelDBList *list, ChannelData *entry, gint pos)
{
    if (dbhandler_db == NULL || list == NULL || entry == NULL)
        return;

    int rc;
    if (fav_insert_entry_stmt == NULL) {
        rc = sqlite3_prepare_v2(dbhandler_db,
                "insert into favourites (chnl_id,list_id,position) values (?,?,?)",
                -1, &fav_insert_entry_stmt, NULL);
        if (rc != SQLITE_OK)
            return;
    }

    sqlite3_bind_int(fav_insert_entry_stmt, 1, entry->id);
    sqlite3_bind_int(fav_insert_entry_stmt, 2, list->id);
    sqlite3_bind_int(fav_insert_entry_stmt, 3, pos);

    rc = sqlite3_step(fav_insert_entry_stmt);
    sqlite3_reset(fav_insert_entry_stmt);
}

void channel_db_list_update_entry(ChannelDBList *list, ChannelData *entry, gint pos)
{
    if (dbhandler_db == NULL || list == NULL || entry == NULL)
        return;

    int rc;
    if (fav_update_entry_stmt == NULL) {
        rc = sqlite3_prepare_v2(dbhandler_db,
                "update favourites set position=? where chnl_id=? and list_id=?",
                -1, &fav_update_entry_stmt, NULL);
        if (rc != SQLITE_OK)
            return;
    }

    sqlite3_bind_int(fav_update_entry_stmt, 1, pos);
    sqlite3_bind_int(fav_update_entry_stmt, 2, entry->id);
    sqlite3_bind_int(fav_update_entry_stmt, 3, list->id);

    rc = sqlite3_step(fav_update_entry_stmt);
    sqlite3_reset(fav_update_entry_stmt);
}

void channel_db_list_remove_entry(ChannelDBList *list, ChannelData *entry)
{
    if (dbhandler_db == NULL || list == NULL || entry == NULL)
        return;

    int rc;
    if (fav_delete_entry_stmt == NULL) {
        rc = sqlite3_prepare_v2(dbhandler_db,
                "delete from favourites where chnl_id=? and list_id=?",
                -1, &fav_delete_entry_stmt, NULL);
        if (rc != SQLITE_OK)
            return;
    }

    sqlite3_bind_int(fav_delete_entry_stmt, 1, entry->id);
    sqlite3_bind_int(fav_delete_entry_stmt, 2, list->id);

    rc = sqlite3_step(fav_delete_entry_stmt);
    sqlite3_reset(fav_delete_entry_stmt);
}

void channel_db_start_transaction(void)
{
    if (dbhandler_db == NULL)
        return;

    sqlite3_exec(dbhandler_db, "BEGIN TRANSACTION", NULL, NULL, NULL);
}

void channel_db_commit_transaction(void)
{
    if (dbhandler_db == NULL)
        return;

    sqlite3_exec(dbhandler_db, "COMMIT TRANSACTION", NULL, NULL, NULL);
}
