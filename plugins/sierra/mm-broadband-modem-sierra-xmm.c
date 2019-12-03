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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-iface-modem.h"
#include "mm-broadband-modem-sierra-xmm.h"
#include "mm-broadband-bearer-sierra-xmm.h"
#include "mm-shared-xmm.h"

static void iface_modem_init (MMIfaceModem *iface);
static void shared_xmm_init  (MMSharedXmm  *iface);
static void iface_modem_signal_init (MMIfaceModemSignal *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSierraXmm, mm_broadband_modem_sierra_xmm, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIGNAL, iface_modem_signal_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_XMM,  shared_xmm_init))


struct _MMBroadbandModemSierraXmmPrivate {
    GRegex *pbready_regex;
    GRegex *ksup_regex;
    GRegex *sim_regex;
    GRegex *nvbu_ind_regex;
    GRegex *xnitzinfo_regex;
    GRegex *ctzdst_regex;
    GRegex *ktempmeas_regex;
    GRegex *cgev_regex;
    GRegex *stkpro_regex;
    GRegex *wdsi_regex;
    GRegex *cssi_regex;
    GRegex *cssu_regex;
    GRegex *cirepi_regex;
    GRegex *cireph_regex;
    GRegex *ciregu_regex;
    GRegex *stkcnf_regex;
    GRegex *xcmt3gpp2_regex;
    GRegex *cusd_regex;
    GRegex *stkcc_regex;
};

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBaseBearer *bearer;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New Sierra XMM bearer created at DBus path '%s'", mm_base_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
broadband_bearer_sierra_xmm_new_ready  (GObject *source,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *simple)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_sierra_xmm_new_finish (res, &error);
    if (!bearer)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    mm_broadband_bearer_sierra_xmm_new (MM_BROADBAND_MODEM_SIERRA_XMM (self),
                                        properties,
                                        NULL, /* cancellable */
                                        (GAsyncReadyCallback)broadband_bearer_sierra_xmm_new_ready,
                                        result);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *_self)
{
    MMBroadbandModemSierraXmm *self = MM_BROADBAND_MODEM_SIERRA_XMM (_self);
    MMPortSerialAt        *ports[2];
    guint                  i;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_sierra_xmm_parent_class)->setup_ports (_self);

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Configure AT ports */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        g_object_set (ports[i],
                      MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                      NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->pbready_regex,
            NULL, NULL, NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ksup_regex,
            NULL, NULL, NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->sim_regex,
            NULL, NULL, NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->nvbu_ind_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->xnitzinfo_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ctzdst_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ktempmeas_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->stkpro_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->wdsi_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->cirepi_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->cireph_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ciregu_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->stkcnf_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->xcmt3gpp2_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->cusd_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->stkcc_regex,
            NULL, NULL, NULL);
    }
}

static void
finalize (GObject *object)
{
    MMBroadbandModemSierraXmm *self = MM_BROADBAND_MODEM_SIERRA_XMM (object);

    g_regex_unref (self->priv->pbready_regex);
    g_regex_unref (self->priv->ksup_regex);
    g_regex_unref (self->priv->sim_regex);
    g_regex_unref (self->priv->nvbu_ind_regex);
    g_regex_unref (self->priv->xnitzinfo_regex);
    g_regex_unref (self->priv->ctzdst_regex);
    g_regex_unref (self->priv->ktempmeas_regex);
    g_regex_unref (self->priv->cgev_regex);
    g_regex_unref (self->priv->stkpro_regex);
    g_regex_unref (self->priv->wdsi_regex);
    g_regex_unref (self->priv->cssi_regex);
    g_regex_unref (self->priv->cssu_regex);
    g_regex_unref (self->priv->cirepi_regex);
    g_regex_unref (self->priv->cireph_regex);
    g_regex_unref (self->priv->ciregu_regex);
    g_regex_unref (self->priv->stkcnf_regex);
    g_regex_unref (self->priv->xcmt3gpp2_regex);
    g_regex_unref (self->priv->cusd_regex);
    g_regex_unref (self->priv->stkcc_regex);

    G_OBJECT_CLASS (mm_broadband_modem_sierra_xmm_parent_class)->finalize (object);
}

/*****************************************************************************/

MMBroadbandModemSierraXmm *
mm_broadband_modem_sierra_xmm_new  (const gchar  *device,
                                    const gchar **drivers,
                                    const gchar  *plugin,
                                    guint16       vendor_id,
                                    guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SIERRA_XMM,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_sierra_xmm_init (MMBroadbandModemSierraXmm *self)
{

    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_SIERRA_XMM,
                                              MMBroadbandModemSierraXmmPrivate);

    /* URCs that break ATA command response */
    self->priv->pbready_regex = g_regex_new ("\\r\\n\\+PBREADY\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ksup_regex = g_regex_new ("\\r\\n\\+KSUP: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->sim_regex = g_regex_new ("\\r\\n\\+SIM: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->nvbu_ind_regex = g_regex_new ("\\r\\n\\+NVBU_IND: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->xnitzinfo_regex = g_regex_new ("\\r\\n\\+XNITZINFO: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ctzdst_regex = g_regex_new ("\\r\\n\\+CTZDST: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->ktempmeas_regex = g_regex_new ("\\r\\n\\+KTEMPMEAS: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->cgev_regex = g_regex_new ("\\r\\n\\+CGEV: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->stkpro_regex = g_regex_new ("\\r\\n\\+STKPRO: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->wdsi_regex = g_regex_new ("\\r\\n\\+WDSI: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->cssi_regex = g_regex_new ("\\r\\n\\+CSSI: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->cssu_regex = g_regex_new ("\\r\\n\\+CSSU: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->cirepi_regex = g_regex_new ("\\r\\n\\+CIREPI: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->cireph_regex = g_regex_new ("\\r\\n\\+CIREPH: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->ciregu_regex = g_regex_new ("\\r\\n\\+CIREGU: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->stkcnf_regex = g_regex_new ("\\r\\n\\+STKCNF: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->xcmt3gpp2_regex = g_regex_new ("\\r\\n\\+XCMT3GPP2: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->cusd_regex = g_regex_new ("\\r\\n\\+CUSD: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->stkcc_regex = g_regex_new ("\\r\\n\\+STKCC: .*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}


static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->create_bearer            = modem_create_bearer;
    iface->create_bearer_finish     = modem_create_bearer_finish;

    iface->load_supported_modes        = mm_shared_xmm_load_supported_modes;
    iface->load_supported_modes_finish = mm_shared_xmm_load_supported_modes_finish;
    iface->load_current_modes          = mm_shared_xmm_load_current_modes;
    iface->load_current_modes_finish   = mm_shared_xmm_load_current_modes_finish;
    iface->set_current_modes           = mm_shared_xmm_set_current_modes;
    iface->set_current_modes_finish    = mm_shared_xmm_set_current_modes_finish;

    iface->load_supported_bands        = mm_shared_xmm_load_supported_bands;
    iface->load_supported_bands_finish = mm_shared_xmm_load_supported_bands_finish;
    iface->load_current_bands          = mm_shared_xmm_load_current_bands;
    iface->load_current_bands_finish   = mm_shared_xmm_load_current_bands_finish;
    iface->set_current_bands           = mm_shared_xmm_set_current_bands;
    iface->set_current_bands_finish    = mm_shared_xmm_set_current_bands_finish;

    iface->load_power_state        = mm_shared_xmm_load_power_state;
    iface->load_power_state_finish = mm_shared_xmm_load_power_state_finish;
    iface->modem_power_up          = mm_shared_xmm_power_up;
    iface->modem_power_up_finish   = mm_shared_xmm_power_up_finish;
    iface->modem_power_down        = mm_shared_xmm_power_down;
    iface->modem_power_down_finish = mm_shared_xmm_power_down_finish;
    iface->modem_power_off         = mm_shared_xmm_power_off;
    iface->modem_power_off_finish  = mm_shared_xmm_power_off_finish;
    iface->reset                   = mm_shared_xmm_reset;
    iface->reset_finish            = mm_shared_xmm_reset_finish;
}

static void
iface_modem_signal_init (MMIfaceModemSignal *iface)
{
    iface->check_support        = mm_shared_xmm_signal_check_support;
    iface->check_support_finish = mm_shared_xmm_signal_check_support_finish;
    iface->load_values          = mm_shared_xmm_signal_load_values;
    iface->load_values_finish   = mm_shared_xmm_signal_load_values_finish;
}

static MMBroadbandModemClass *
peek_parent_broadband_modem_class (MMSharedXmm *self)
{
    return MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_sierra_xmm_parent_class);
}

static MMIfaceModemLocation *
peek_parent_location_interface (MMSharedXmm *self)
{
    return NULL;
}

static void
shared_xmm_init (MMSharedXmm *iface)
{
    iface->peek_parent_broadband_modem_class = peek_parent_broadband_modem_class;
    iface->peek_parent_location_interface    = peek_parent_location_interface;
}

static void
mm_broadband_modem_sierra_xmm_class_init (MMBroadbandModemSierraXmmClass *klass)
{
    GObjectClass          *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemSierraXmmPrivate));

    broadband_modem_class->setup_ports = setup_ports;
    object_class->finalize = finalize;
}
