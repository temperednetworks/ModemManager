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
 */

#include <config.h>

#include "mm-broadband-modem-quectel.h"
#include "mm-shared-quectel.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-3gpp.h"


static void shared_quectel_init       (MMSharedQuectel      *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);
static void iface_modem_3gpp_init     (MMIfaceModem3gpp     *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQuectel, mm_broadband_modem_quectel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QUECTEL, shared_quectel_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

struct _MMBroadbandModemQuectelPrivate {
    /* Unsolicited messaging setup */
    MMSharedQuectelUnsolicitedSetup *unsolicited_setup;
};

static void
mm_broadband_modem_quectel_init (MMBroadbandModemQuectel *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_QUECTEL,
                                              MMBroadbandModemQuectelPrivate);
    self->priv->unsolicited_setup = mm_shared_quectel_unsolicited_setup_new ();
}

static void
finalize (GObject *object)
{
    MMBroadbandModemQuectel *self = MM_BROADBAND_MODEM_QUECTEL (object);

    mm_shared_quectel_unsolicited_setup_free (self->priv->unsolicited_setup);

    G_OBJECT_CLASS (mm_broadband_modem_quectel_parent_class)->finalize (object);
}


/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else {
        /* Our own setup now */
        mm_shared_quectel_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self),
                                                           MM_BROADBAND_MODEM_QUECTEL (self)->priv->unsolicited_setup,
                                                           TRUE);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_setup_unsolicited_events_ready,
        task);
}

static void
parent_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Our own cleanup first */
    mm_shared_quectel_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self),
                                                       MM_BROADBAND_MODEM_QUECTEL (self)->priv->unsolicited_setup,
                                                       FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_quectel_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle when enabled */
    mm_shared_quectel_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self),
                                                       MM_BROADBAND_MODEM_QUECTEL (self)->priv->unsolicited_setup,
                                                       FALSE);
}

/*****************************************************************************/

MMBroadbandModemQuectel *
mm_broadband_modem_quectel_new (const gchar  *device,
                                const gchar **drivers,
                                const gchar  *plugin,
                                guint16       vendor_id,
                                guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QUECTEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
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
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
}

static void
mm_broadband_modem_quectel_class_init (MMBroadbandModemQuectelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof(MMBroadbandModemQuectelPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
