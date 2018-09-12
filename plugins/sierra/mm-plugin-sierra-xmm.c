/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2017 - 2018 Tempered Networks Inc.
 *
 */

#include <stdlib.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-plugin-sierra-xmm.h"
#include "mm-broadband-modem.h"
#include "mm-broadband-modem-xmm.h"
#include "mm-broadband-modem-mbim-xmm.h"



G_DEFINE_TYPE (MMPluginSierraXmm, mm_plugin_sierra_xmm, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{



    if (mm_port_probe_list_is_xmm (probes)) {
    #if defined WITH_MBIM
        if (mm_port_probe_list_has_mbim_port (probes)) {
            mm_dbg ("MBIM-powered XMM-based Sierra modem found...");
            return MM_BASE_MODEM (mm_broadband_modem_mbim_xmm_new (uid,
                                                                   drivers,
                                                                   mm_plugin_get_name (self),
                                                                   vendor,
                                                                   product));
        }
    #endif
        mm_dbg ("XMM-based Sierra modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_xmm_new   (uid,
                                                            drivers,
                                                            mm_plugin_get_name (self),
                                                            vendor,
                                                            product));
    }


    /* Fallback to default modem in the worst case */
    return MM_BASE_MODEM (mm_broadband_modem_new (uid,
                                                  drivers,
                                                  mm_plugin_get_name (self),
                                                  vendor,
                                                  product));
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const guint16 vendor_ids[] = { 0x1519, 0x8087, 0 };
    static const gchar *drivers[] = { "cdc_acm", "cdc_ncm", "cdc_mbim", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_SIERRA_XMM,
                      MM_PLUGIN_NAME,               "Sierra XMM",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_ALLOWED_DRIVERS,    drivers,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,       TRUE,
                      MM_PLUGIN_XMM_PROBE,          TRUE,
                      NULL));
}

static void
mm_plugin_sierra_xmm_init (MMPluginSierraXmm *self)
{
}

static void
mm_plugin_sierra_xmm_class_init (MMPluginSierraXmmClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
