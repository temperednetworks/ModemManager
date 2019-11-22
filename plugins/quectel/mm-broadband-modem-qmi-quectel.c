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

#include "mm-broadband-modem-qmi-quectel.h"
#include "mm-shared-quectel.h"
#include "mm-shared-qmi.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-log.h"

static void shared_quectel_init       (MMSharedQuectel      *iface);
static void iface_modem_init          (MMIfaceModem         *iface);
static void shared_qmi_init           (MMSharedQmi          *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmiQuectel, mm_broadband_modem_qmi_quectel, MM_TYPE_BROADBAND_MODEM_QMI, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QUECTEL, shared_quectel_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QMI, shared_qmi_init))

/*****************************************************************************/

struct _MMBroadbandModemQmiQuectelPrivate {
    /* Unsolicited messaging setup */
    MMSharedQuectelUnsolicitedSetup *unsolicited_setup;
};

static void
mm_broadband_modem_qmi_quectel_init (MMBroadbandModemQmiQuectel *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL,
                                              MMBroadbandModemQmiQuectelPrivate);
    self->priv->unsolicited_setup = mm_shared_quectel_unsolicited_setup_new ();
}

static void
finalize (GObject *object)
{
    MMBroadbandModemQmiQuectel *self = MM_BROADBAND_MODEM_QMI_QUECTEL (object);

    mm_shared_quectel_unsolicited_setup_free (self->priv->unsolicited_setup);

    G_OBJECT_CLASS (mm_broadband_modem_qmi_quectel_parent_class)->finalize (object);
}

MMBroadbandModemQmiQuectel *
mm_broadband_modem_qmi_quectel_new (const gchar  *device,
                                    const gchar **drivers,
                                    const gchar  *plugin,
                                    guint16       vendor_id,
                                    guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

/*****************************************************************************/
/* Manufacturer loading (Modem interface) */

static gchar *
modem_load_manufacturer_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    const gchar *result;
    gchar *manufacturer = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (result) {
        manufacturer = g_strdup (result);
        mm_dbg ("loaded manufacturer: %s", manufacturer);
    }
    return manufacturer;
}

static void
modem_load_manufacturer (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    mm_dbg ("loading manufacturer...");
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CGMI",
        3,
        FALSE,
        callback,
        user_data);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_quectel_parent_class)->setup_ports (self);

    /* Now setup NULL URC handlers for nuisance URCs. */
    mm_shared_quectel_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self),
                                                       MM_BROADBAND_MODEM_QMI_QUECTEL (self)->priv->unsolicited_setup);
}

/*****************************************************************************/
/* Model loading (Modem interface) */

static gchar *
modem_load_model_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    const gchar *result;
    gchar *model = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (result) {
        model = g_strdup (result);
        mm_dbg ("loaded model: %s", model);
    }
    return model;
}

static void
modem_load_model (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    mm_dbg ("loading model...");
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CGMM",
        3,
        FALSE,
        callback,
        user_data);
}

/*****************************************************************************/
/* Reset (Modem interface) */

static gboolean
modem_qmi_reset_finish (MMIfaceModem  *self,
                        GAsyncResult  *res,
                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
reset_set_operating_mode_reset_ready (QmiClientDms *client,
                                      GAsyncResult *res,
                                      GTask *task)
{
    QmiMessageDmsSetOperatingModeOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output || !qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        g_task_return_error (task, error);
    } else {
        mm_info ("Modem is being rebooted now");
        g_task_return_boolean (task, TRUE);
    }

    if (output)
        qmi_message_dms_set_operating_mode_output_unref (output);

    g_object_unref (task);
}

static void
modem_qmi_reset (MMIfaceModem        *self,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    QmiMessageDmsSetOperatingModeInput *input;
    GTask                              *task;
    QmiClient                          *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Now, go into offline mode */
    input = qmi_message_dms_set_operating_mode_input_new ();
    qmi_message_dms_set_operating_mode_input_set_mode (input, QMI_DMS_OPERATING_MODE_RESET, NULL);
    qmi_client_dms_set_operating_mode (QMI_CLIENT_DMS (client),
                                       input,
                                       20,
                                       NULL,
                                       (GAsyncReadyCallback)reset_set_operating_mode_reset_ready,
                                       task);
    qmi_message_dms_set_operating_mode_input_unref (input);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_manufacturer = modem_load_manufacturer;
    iface->load_manufacturer_finish = modem_load_manufacturer_finish;
    iface->load_model = modem_load_model;
    iface->load_model_finish = modem_load_model_finish;
    iface->reset = modem_qmi_reset;
    iface->reset_finish = modem_qmi_reset_finish;
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
shared_qmi_init (MMSharedQmi *iface)
{
}

static void
mm_broadband_modem_qmi_quectel_class_init (MMBroadbandModemQmiQuectelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemQmiQuectelPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
