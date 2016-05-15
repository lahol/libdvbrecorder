#pragma once

#include <glib.h>
#include <time.h>

typedef enum {
    EPGEventStatusUndefined = 0,
    EPGEventStatusNotRunning,
    EPGEventStatusStartsInFewSeconds,
    EPGEventStatusPausing,
    EPGEventStatusRunning,
    EPGEventStatusServiceOffAir,
    EPGEventStatusReserved1,
    EPGEventStatusReserved2
} EPGEventRunningStatus;

typedef struct {
    gchar language[4];
    gchar *description;
    gchar *text;
} EPGShortEvent;

typedef struct {
    gchar *description;
    gchar *content;
} EPGExtendedEventItem;

typedef struct {
    gchar language[4];
    GList *description_items;
    gchar *text;
} EPGExtendedEvent;

typedef struct {
    guint16 event_id;
    guint8 table_id;
    EPGEventRunningStatus running_status;
    time_t starttime;
    guint32 duration;
    GList *short_descriptions;     /* list of multiple (language-specific?) descriptions */
    GList *extended_descriptions;  /* list of multiple (language-specific?) descriptions, descriptors concatenated */
} EPGEvent;

void epg_event_free(EPGEvent *event);
gint epg_event_compare_time(EPGEvent *a, EPGEvent *b);

EPGEvent *epg_event_dup(EPGEvent *event);
GList *epg_event_list_dup(GList *list);

