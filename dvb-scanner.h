#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define DVB_SCANNER_TYPE (dvb_scanner_get_type())
#define DVB_SCANNER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), DVB_SCANNER_TYPE, DVBScanner))
#define IS_DVB_SCANNER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), DVB_SCANNER_TYPE))
#define DVB_SCANNER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), DVB_SCANNER_TYPE, DVBScannerClass))
#define IS_DVB_SCANNER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), DVB_SCANNER_TYPE))
#define DVB_SCANNER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), DVB_SCANNER_TYPE, DVBScannerClass))

typedef struct _DVBScanner DVBScanner;
typedef struct _DVBScannerPrivate DVBScannerPrivate;
typedef struct _DVBScannerClass DVBScannerClass;

struct _DVBScanner {
    GObject parent_instance;

    /*< private >*/
    DVBScannerPrivate *priv;
};

struct _DVBScannerClass {
    GObjectClass parent_class;
};

GType dvb_scanner_get_type(void) G_GNUC_CONST;

DVBScanner *dvb_scanner_new(void);

void dvb_scanner_set_scan_command(DVBScanner *scanner, const gchar *scan_command);
const gchar *dvb_scanner_get_scan_command(DVBScanner *scanner);
void dvb_scanner_set_satellite(DVBScanner *scanner, const gchar *satellite);
const gchar *dvb_scanner_get_satellite(DVBScanner *scanner);

void dvb_scanner_start(DVBScanner *scanner);
void dvb_scanner_stop(DVBScanner *scanner);

void dvb_scanner_update_channels_db(DVBScanner *scanner);

G_END_DECLS
