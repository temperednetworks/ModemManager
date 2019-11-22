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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2019 Tempered Networks Inc
 */

#include <config.h>

#include "mm-broadband-modem-mbim-quectel.h"
#include "mm-shared-quectel.h"
#include "mm-iface-modem-firmware.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-log.h"

static void shared_quectel_init       (MMSharedQuectel      *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);


G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimQuectel, mm_broadband_modem_mbim_quectel, MM_TYPE_BROADBAND_MODEM_MBIM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QUECTEL, shared_quectel_init))

/*****************************************************************************/

struct _MMBroadbandModemMbimQuectelPrivate {
    /* Unsolicited messaging setup */
    MMSharedQuectelUnsolicitedSetup *unsolicited_setup;
};

static void
mm_broadband_modem_mbim_quectel_init (MMBroadbandModemMbimQuectel *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL,
                                              MMBroadbandModemMbimQuectelPrivate);
    self->priv->unsolicited_setup = mm_shared_quectel_unsolicited_setup_new ();
}

static void
finalize (GObject *object)
{
    MMBroadbandModemMbimQuectel *self = MM_BROADBAND_MODEM_MBIM_QUECTEL (object);

    mm_shared_quectel_unsolicited_setup_free (self->priv->unsolicited_setup);

    G_OBJECT_CLASS (mm_broadband_modem_mbim_quectel_parent_class)->finalize (object);
}

MMBroadbandModemMbimQuectel *
mm_broadband_modem_mbim_quectel_new (const gchar  *device,
                                     const gchar **drivers,
                                     const gchar  *plugin,
                                     guint16       vendor_id,
                                     guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_quectel_parent_class)->setup_ports (self);

    /* Now setup NULL URC handlers for nuisance URCs. */
    mm_shared_quectel_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self),
                                                       MM_BROADBAND_MODEM_MBIM_QUECTEL (self)->priv->unsolicited_setup);
}

static void
iface_modem_firmware_init (MMIfaceModemFirmware *iface)
{
    iface->load_update_settings = mm_shared_quectel_firmware_load_update_settings;
    iface->load_update_settings_finish = mm_shared_quectel_firmware_load_update_settings_finish;
}

static void
shared_quectel_init (MMSharedQuectel *iface)
{
}

static void
mm_broadband_modem_mbim_quectel_class_init (MMBroadbandModemMbimQuectelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemMbimQuectelPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
