#pragma once

#include <glib.h>
/* #include <dvbpsi/dvbpsi.h>
 * #include <dvbpsi/descriptor.h>
 * #include <dvbpsi/eit.h>
 */

GList *epg_read_table(dvbpsi_eit_t *eit);
