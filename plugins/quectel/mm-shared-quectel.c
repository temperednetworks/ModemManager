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

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem-firmware.h"
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-shared-quectel.h"

/*****************************************************************************/
/* Firmware update settings loading (Firmware interface) */

MMFirmwareUpdateSettings *
mm_shared_quectel_firmware_load_update_settings_finish (MMIfaceModemFirmware  *self,
                                                        GAsyncResult          *res,
                                                        GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
qfastboot_test_ready (MMBaseModem  *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMFirmwareUpdateSettings *update_settings;

    if (!mm_base_modem_at_command_finish (self, res, NULL))
        update_settings = mm_firmware_update_settings_new (MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE);
    else {
        update_settings = mm_firmware_update_settings_new (MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT);
        mm_firmware_update_settings_set_fastboot_at (update_settings, "AT+QFASTBOOT");
    }

    g_task_return_pointer (task, update_settings, g_object_unref);
    g_object_unref (task);
}

void
mm_shared_quectel_firmware_load_update_settings (MMIfaceModemFirmware *self,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT+QFASTBOOT=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)qfastboot_test_ready,
                              task);
}

/*****************************************************************************/
/* Unsolicited result codes */

struct _MMSharedQuectelUnsolicitedSetup {
    /* URCs to ignore*/
    GRegex *qind_regex; /* SMS / PB related */
    GRegex *qodm_regex; /* OMA-DM related */
    GRegex *qusim_regex; /* SIM card state URC */
    GRegex *rdy_regex;
    GRegex *cbm_regex;  /* SMS related */
};


MMSharedQuectelUnsolicitedSetup *
mm_shared_quectel_unsolicited_setup_new (void)
{
    MMSharedQuectelUnsolicitedSetup *setup;

    setup = g_new0 (MMSharedQuectelUnsolicitedSetup, 1);

    /* Prepare regular expressions to setup */
    setup->qind_regex = g_regex_new ("\\r\\n\\+QIND:(.*)\\r\\n",
                                       G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->qind_regex != NULL);

    setup->qodm_regex = g_regex_new ("\\r\\n\\+QODM: (.*)\\r\\n",
                                      G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->qodm_regex != NULL);

    setup->qusim_regex = g_regex_new ("\\r\\n\\+QUSIM: (.*)\\r\\n",
                                      G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->qusim_regex != NULL);

    setup->rdy_regex = g_regex_new ("\\r\\nRDY\\r\\n",
                                      G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->rdy_regex != NULL);

    setup->cbm_regex = g_regex_new ("\\r\\n\\+CBM: (.*)\\r\\n",
                                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->cbm_regex != NULL);

    return setup;
}

void
mm_shared_quectel_unsolicited_setup_free (MMSharedQuectelUnsolicitedSetup *setup)
{
    g_regex_unref (setup->qind_regex);
    g_regex_unref (setup->qodm_regex);
    g_regex_unref (setup->qusim_regex);
    g_regex_unref (setup->rdy_regex);
    g_regex_unref (setup->cbm_regex);
    g_free (setup);
}

void
mm_shared_quectel_set_unsolicited_events_handlers (MMBroadbandModem *self,
                                                   MMSharedQuectelUnsolicitedSetup *setup,
                                                   gboolean enable)
{
    MMPortSerialAt *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        /* Other unsolicited events to always ignore */
        if (!enable) {
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->qind_regex,
                NULL, NULL, NULL);
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->qodm_regex,
                NULL, NULL, NULL);
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->qusim_regex,
                NULL, NULL, NULL);
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->rdy_regex,
                NULL, NULL, NULL);
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->cbm_regex,
                NULL, NULL, NULL);
        }
    }
}

/*****************************************************************************/

static void
shared_quectel_init (gpointer g_iface)
{
}

GType
mm_shared_quectel_get_type (void)
{
    static GType shared_quectel_type = 0;

    if (!G_UNLIKELY (shared_quectel_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedQuectel),  /* class_size */
            shared_quectel_init,       /* base_init */
            NULL,                      /* base_finalize */
        };

        shared_quectel_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedQuectel", &info, 0);
        g_type_interface_add_prerequisite (shared_quectel_type, MM_TYPE_IFACE_MODEM_FIRMWARE);
    }

    return shared_quectel_type;
}
