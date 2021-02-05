#include "dvb-scanner.h"
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include "channels.h"
#include "channel-db.h"
#include <sys/types.h>
#include <signal.h>

struct _DVBScannerPrivate {
    /* private data */
    gchar *scan_command;
    gchar *satellite;

    GMutex scan_mutex;

    GList *scanned_channels;
    GList *scanned_satellites;

    guint32 status_scanning : 1;

    guint32 channels_found;

    GPid child_pid;
};

G_DEFINE_TYPE(DVBScanner, dvb_scanner, G_TYPE_OBJECT);

enum {
    PROP_0,
    PROP_SCAN_COMMAND,
    PROP_SATELLITE,
    N_PROPERTIES
};

enum {
    SIGNAL_SCAN_STARTED = 0,
    SIGNAL_SCAN_FINISHED,
    SIGNAL_CHANNEL_FOUND,
    N_SIGNALS
};

guint dvb_scanner_signals[N_SIGNALS];

static void _dvb_scanner_copy_to_list(ChannelData *data, GList **list)
{
    if (list)
        *list = g_list_prepend(*list, channel_data_dup(data));
}

static gint _dvb_scanner_compare_channel_nid_sid(ChannelData *a, ChannelData *b)
{
    if (a->nid == b->nid && a->sid == b->sid)
        return 0;
    if (a->nid > b->nid)
        return 1;
    else if (a->nid < b->nid)
        return -1;
    if (a->sid > b->sid)
        return 1;
    else if (a->sid < b->sid)
        return -1;
    return 1;
}

static gboolean _dvb_scanner_is_scanned_satellite(ChannelData *data, GList *scanned_satellites)
{
    if (data == NULL || scanned_satellites == NULL)
        return FALSE;
    if (data->signalsource == NULL || !(*(data->signalsource)))
        return FALSE;

    static const gchar *translations[] = {
        "S19E2", "S19.2E",
        "S23E5", "S23.E5",
        "S13E0", "S13E",
        "S28E2", "S28.2E",
        "S9E0", "S9.0E",
        NULL, NULL
    };

    GList *tmp;
    guint i;
    for (i = 0; translations[i] != NULL; i += 2) {
        if (g_strcmp0(data->signalsource, translations[i+1]) == 0) {
            for (tmp = scanned_satellites; tmp != NULL; tmp = g_list_next(tmp)) {
                if (g_strcmp0((gchar *)tmp->data, translations[i]) == 0)
                    return TRUE;
            }
            break;
        }
    }

    return FALSE;
}

void dvb_scanner_update_channels_db(DVBScanner *scanner)
{
    GList *old_channels = NULL;
    if (scanner->priv->scanned_satellites == NULL)
        return;

    GList *tmp, *match;
    GList *new_channels = NULL;

    /* get all channels from db */
    channel_db_foreach(0, (CHANNEL_DB_FOREACH_CALLBACK)_dvb_scanner_copy_to_list, &old_channels);

    /* if old channel comes from one of the scanned channels, mark as dirty */
    for (tmp = old_channels; tmp != NULL; tmp = g_list_next(tmp)) {
        if (_dvb_scanner_is_scanned_satellite((ChannelData *)tmp->data,
                    scanner->priv->scanned_satellites)) {
            ((ChannelData *)tmp->data)->flags |= CHNL_FLAG_DIRTY;
        }
    }

    /* for each scanned channel, look up in list if nid, sid match
     *  if found: update data, mark clean
     *  else: append to new_channels
     */
    for (tmp = scanner->priv->scanned_channels; tmp != NULL; tmp = g_list_next(tmp)) {
        match = g_list_find_custom(old_channels, tmp->data, (GCompareFunc)_dvb_scanner_compare_channel_nid_sid);
        if (match != NULL) {
            channel_data_update_payload((ChannelData *)match->data, (ChannelData *)tmp->data);
            ((ChannelData *)match->data)->flags &= ~CHNL_FLAG_DIRTY;
        }
        else {
            new_channels = g_list_prepend(new_channels, channel_data_dup((ChannelData *)tmp->data));
        }
    }

    /* write lists back to db */
    channel_db_start_transaction();
    for (tmp = old_channels; tmp != NULL; tmp = g_list_next(tmp)) {
        channel_db_set_channel((ChannelData *)tmp->data);
    }
    for (tmp = new_channels; tmp != NULL; tmp = g_list_next(tmp)) {
        channel_db_set_channel((ChannelData *)tmp->data);
    }
    channel_db_commit_transaction();

    g_list_free_full(old_channels, (GDestroyNotify)channel_data_free);
    g_list_free_full(new_channels, (GDestroyNotify)channel_data_free);
}

static void dvb_scanner_child_watch_cb(GPid pid, gint status, DVBScanner *self)
{
    fprintf(stderr, "dvb_scanner_child_watch_cb: %u (%d)\n", pid, status);
    g_spawn_close_pid(pid);
    self->priv->child_pid = 0;

    /* do net emit signal if object is being destructed */
    if (IS_DVB_SCANNER(self))
        g_signal_emit(self, dvb_scanner_signals[SIGNAL_SCAN_FINISHED], 0, G_TYPE_NONE);
}

static gboolean dvb_scanner_watch_out_cb(GIOChannel *channel, GIOCondition cond, DVBScanner *self)
{
    gchar *string = NULL;
    gsize size;

    if (cond == G_IO_HUP) {
        g_io_channel_unref(channel);
        return FALSE;
    }

    g_io_channel_read_line(channel, &string, &size, NULL, NULL);

    g_mutex_lock(&self->priv->scan_mutex);
    ChannelData *data = channel_data_parse(string, self->priv->satellite);
    if (data != NULL) {
        self->priv->scanned_channels = g_list_prepend(self->priv->scanned_channels, data);

        g_signal_emit(self, dvb_scanner_signals[SIGNAL_CHANNEL_FOUND], 0, data);
    }
    g_mutex_unlock(&self->priv->scan_mutex);

    g_free(string);

    return TRUE;
}

void dvb_scanner_start(DVBScanner *scanner)
{
    g_return_if_fail(IS_DVB_SCANNER(scanner));
    g_return_if_fail(scanner->priv->scan_command != NULL && *(scanner->priv->scan_command));
    g_return_if_fail(scanner->priv->satellite != NULL && *(scanner->priv->satellite));

    GRegex *cmd_regex = g_regex_new("\\${satellite}", G_REGEX_RAW, 0, NULL);
    gchar *cmd = g_regex_replace_literal(cmd_regex, scanner->priv->scan_command,
            -1, 0, scanner->priv->satellite, 0, NULL);

    fprintf(stderr, "Command: %s\n", cmd);

    gint argc = 0;
    gchar **argv = NULL;

    if (!g_shell_parse_argv(cmd, &argc, &argv, NULL))
        goto done;

    scanner->priv->scanned_satellites = g_list_prepend(scanner->priv->scanned_satellites,
                                                       g_strdup(scanner->priv->satellite));

    GIOChannel *out_ch;
    gint out;
    gboolean ret;

    ret = g_spawn_async_with_pipes(NULL, argv, NULL,
            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL,
            NULL, &scanner->priv->child_pid, NULL, &out, NULL, NULL);

    fprintf(stderr, "spawn ret: %d\n", ret);

    if (!ret)
        goto done;

    g_signal_emit(scanner, dvb_scanner_signals[SIGNAL_SCAN_STARTED], 0, G_TYPE_NONE);

    g_child_watch_add(scanner->priv->child_pid, (GChildWatchFunc)dvb_scanner_child_watch_cb, scanner);

    out_ch = g_io_channel_unix_new(out);
    g_io_add_watch(out_ch, G_IO_IN | G_IO_HUP, (GIOFunc)dvb_scanner_watch_out_cb, scanner);

done:
    g_strfreev(argv);
    g_regex_unref(cmd_regex);
    g_free(cmd);
}

void dvb_scanner_stop(DVBScanner *scanner)
{
    fprintf(stderr, "dvb_scanner_stop (%p)\n", scanner);
    g_return_if_fail(IS_DVB_SCANNER(scanner));

    fprintf(stderr, "child pid: %u\n", scanner->priv->child_pid);

    if (scanner->priv->child_pid > 0) {
        kill(scanner->priv->child_pid, SIGKILL);
        scanner->priv->child_pid = 0;
    }
}

static void dvb_scanner_dispose(GObject *gobject)
{
    DVBScanner *self = DVB_SCANNER(gobject);
    dvb_scanner_stop(self);

    g_list_free_full(self->priv->scanned_channels,
            (GDestroyNotify)channel_data_free);
    self->priv->scanned_channels = NULL;

    g_list_free_full(self->priv->scanned_satellites,
            (GDestroyNotify)g_free);
    self->priv->scanned_satellites = NULL;

    G_OBJECT_CLASS(dvb_scanner_parent_class)->dispose(gobject);
}

static void dvb_scanner_finalize(GObject *gobject)
{
    DVBScanner *self = DVB_SCANNER(gobject);
    g_free(self->priv->scan_command);
    g_free(self->priv->satellite);

    self->priv->satellite = NULL;
    self->priv->scan_command = NULL;

    G_OBJECT_CLASS(dvb_scanner_parent_class)->finalize(gobject);
}

static void dvb_scanner_set_property(GObject *object, guint prop_id,
        const GValue *value, GParamSpec *spec)
{
    DVBScanner *self = DVB_SCANNER(object);

    switch (prop_id) {
        case PROP_SCAN_COMMAND:
            dvb_scanner_set_scan_command(self, g_value_get_string(value));
            break;
        case PROP_SATELLITE:
            dvb_scanner_set_satellite(self, g_value_get_string(value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, spec);
            break;
    }
}

static void dvb_scanner_get_property(GObject *object, guint prop_id,
        GValue *value, GParamSpec *spec)
{
    DVBScanner *self = DVB_SCANNER(object);

    switch (prop_id) {
    	case PROP_SCAN_COMMAND:
            g_value_set_string(value, self->priv->scan_command);
            break;
        case PROP_SATELLITE:
            g_value_set_string(value, self->priv->satellite);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, spec);
            break;
    }
}

static void dvb_scanner_class_init(DVBScannerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    /* override GObject methods */
    gobject_class->dispose = dvb_scanner_dispose;
    gobject_class->finalize = dvb_scanner_finalize;
    gobject_class->set_property = dvb_scanner_set_property;
    gobject_class->get_property = dvb_scanner_get_property;

    g_object_class_install_property(gobject_class,
            PROP_SCAN_COMMAND,
            g_param_spec_string("scan-command",
                "Scan command",
                "The command line used for scanning",
                NULL,
                G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class,
            PROP_SATELLITE,
            g_param_spec_string("satellite",
                "Satellite",
                "The satellite for which the scanning is performed",
                NULL,
                G_PARAM_READWRITE));

    dvb_scanner_signals[SIGNAL_SCAN_STARTED] =
        g_signal_new("scan-started",
                G_TYPE_FROM_CLASS(gobject_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL,
                NULL,
                NULL,
                G_TYPE_NONE,
                0,
                NULL);
    dvb_scanner_signals[SIGNAL_SCAN_FINISHED] =
        g_signal_new("scan-finished",
                G_TYPE_FROM_CLASS(gobject_class),
                G_SIGNAL_RUN_FIRST,
                0,
                NULL,
                NULL,
                NULL,
                G_TYPE_NONE,
                0,
                NULL);
    dvb_scanner_signals[SIGNAL_CHANNEL_FOUND] =
        g_signal_new("channel-found",
                G_TYPE_FROM_CLASS(gobject_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL,
                NULL,
                NULL,
                G_TYPE_NONE,
                1,
                G_TYPE_POINTER,
                NULL);

    g_type_class_add_private(G_OBJECT_CLASS(klass), sizeof(DVBScannerPrivate));
}

static void dvb_scanner_init(DVBScanner *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
            DVB_SCANNER_TYPE, DVBScannerPrivate);

    g_mutex_init(&self->priv->scan_mutex);
}

DVBScanner *dvb_scanner_new(void)
{
    return g_object_new(DVB_SCANNER_TYPE, NULL);
}

void dvb_scanner_set_scan_command(DVBScanner *scanner, const gchar *scan_command)
{
    g_return_if_fail(IS_DVB_SCANNER(scanner));
    g_free(scanner->priv->scan_command);
    scanner->priv->scan_command = g_strdup(scan_command);
}

const gchar *dvb_scanner_get_scan_command(DVBScanner *scanner)
{
    g_return_val_if_fail(IS_DVB_SCANNER(scanner), NULL);
    return NULL;
}

void dvb_scanner_set_satellite(DVBScanner *scanner, const gchar *satellite)
{
    g_return_if_fail(IS_DVB_SCANNER(scanner));
    g_free(scanner->priv->satellite);
    scanner->priv->satellite = g_strdup(satellite);
}

const gchar *dvb_scanner_get_satellite(DVBScanner *scanner)
{
    g_return_val_if_fail(IS_DVB_SCANNER(scanner), NULL);
    return NULL;
}


