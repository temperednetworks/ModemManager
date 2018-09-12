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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2017 - 2018 Tempered Networks Inc.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-sierra-xmm.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-xmm.h"

G_DEFINE_TYPE (MMBroadbandBearerSierraXmm, mm_broadband_bearer_sierra_xmm, MM_TYPE_BROADBAND_BEARER)


/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef enum {
    DIAL_3GPP_CONTEXT_STEP_FIRST = 0,
    DIAL_3GPP_CONTEXT_STEP_CHECK_CGACT,
    DIAL_3GPP_CONTEXT_STEP_SET_WPPP,
    DIAL_3GPP_CONTEXT_STEP_SET_XDNS,
    DIAL_3GPP_CONTEXT_STEP_SET_CGACT,
    DIAL_3GPP_CONTEXT_STEP_SET_DATA_CHAN,
    DIAL_3GPP_CONTEXT_STEP_START_DATA_CHAN,
    DIAL_3GPP_CONTEXT_STEP_LAST
} Dial3gppContextStep;

typedef struct {
    MMBaseModem                 *modem;
    MMPortSerialAt              *primary;
    MMPortSerialAt              *secondary;
    guint                       cid;
    MMBearerIpFamily            ip_family;
    MMPort                      *data;
    gint                        usb_interface_config_index;
    gint                        usb_acm_config_index;
    Dial3gppContextStep         step;
} Dial3gppContext;

static void
dial_3gpp_context_free (Dial3gppContext *ctx)
{
    g_clear_object (&ctx->modem);
    g_clear_object (&ctx->primary);
    g_clear_object (&ctx->secondary);
    g_clear_object (&ctx->data);
    g_slice_free (Dial3gppContext, ctx);
}

/* Forward Function declarations */
static void dial_3gpp_context_step (GTask *task);

static MMPort *
dial_3gpp_finish (MMBroadbandBearer  *self,
                  GAsyncResult       *res,
                  GError            **error)
{
    return MM_PORT (g_task_propagate_pointer (G_TASK (res), error));
}

static void
dial_3gpp_cgact_check_ready (MMBaseModem  *modem,
                             GAsyncResult *res,
                             GTask        *task)
{
    const gchar             *response;
    Dial3gppContext         *ctx;
    GError                  *error = NULL;
    GList                   *cgact_list = NULL;
    GList                   *l = NULL;
    MM3gppPdpContextActive  *pdp_ctx_active = NULL;

    ctx = (Dial3gppContext *) g_task_get_task_data (task);

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Parse response */
    cgact_list = mm_3gpp_parse_cgact_read_response (response, &error);
    g_assert(cgact_list != NULL);

    if (!cgact_list) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Iterate over list and check for active context with same CID */
    for (l = cgact_list; l; l = g_list_next(l)) {
        pdp_ctx_active = l->data;
        if (pdp_ctx_active->cid == ctx->cid) {
            break;
        }
        else
            pdp_ctx_active = NULL;
    }

    /* If context is already active, skip to setting up the datachannel */
    if (pdp_ctx_active != NULL && pdp_ctx_active->active){
        mm_dbg("Selected CID is currently active. Skipping to data channel command.");
        ctx->step = DIAL_3GPP_CONTEXT_STEP_SET_DATA_CHAN;
    }
    /* Otherwise, skip to AUTH setup */
    else
        ctx->step = DIAL_3GPP_CONTEXT_STEP_SET_WPPP;

    mm_3gpp_pdp_context_active_list_free (cgact_list);
    dial_3gpp_context_step (task);
}

typedef enum {
    BEARER_XMM_AUTH_NONE = 0,
    BEARER_XMM_AUTH_PAP  = 1,
    BEARER_XMM_AUTH_CHAP = 2
} BearerInfineonAuthType;

/* Generate WPPP string */
static gchar *
build_auth_string (GTask *task)
{
    Dial3gppContext             *ctx;
    gchar                       *command;
    const gchar                 *user;
    const gchar                 *password;
    MMBearerAllowedAuth         auth_type;
    MMBroadbandBearerSierraXmm  *self;

    ctx = (Dial3gppContext *) g_task_get_task_data (task);
    self = (MMBroadbandBearerSierraXmm *) g_task_get_source_object(task);
    user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    password = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    auth_type = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));

    /* Both user and password are required; otherwise firmware returns an error */
    if (user || password) {
        gchar *encoded_user = NULL;
        gchar *encoded_password = NULL;
        BearerInfineonAuthType auth_type_inf = BEARER_XMM_AUTH_NONE;

        /* Prefer CHAP over PAP */
        if (auth_type & MM_BEARER_ALLOWED_AUTH_CHAP)
            auth_type_inf = BEARER_XMM_AUTH_CHAP;
        else if (auth_type & MM_BEARER_ALLOWED_AUTH_PAP)
            auth_type_inf = BEARER_XMM_AUTH_PAP;
        else if (auth_type == MM_BEARER_ALLOWED_AUTH_UNKNOWN)
            auth_type_inf = BEARER_XMM_AUTH_CHAP;
        else if (auth_type == MM_BEARER_ALLOWED_AUTH_NONE)
            auth_type_inf = BEARER_XMM_AUTH_NONE;

        encoded_user = mm_broadband_modem_take_and_convert_to_current_charset (MM_BROADBAND_MODEM (ctx->modem),
                                                                               g_strdup (user));
        encoded_password = mm_broadband_modem_take_and_convert_to_current_charset (MM_BROADBAND_MODEM (ctx->modem),
                                                                                   g_strdup (password));

        command = g_strdup_printf ("+WPPP=%u,%u,\"%s\",\"%s\"",
                                   auth_type_inf,
                                   ctx->cid,
                                   encoded_user ? encoded_user : "",
                                   encoded_password ? encoded_password : "");
        g_free (encoded_user);
        g_free (encoded_password);
    } else {
        command = g_strdup_printf ("+WPPP=0,%u,\"\",\"\"", ctx->cid);
    }
    return command;
}

/* Generic AT command result ready callback */
static void
dial_3gpp_res_ready (MMBaseModem *modem,
                     GAsyncResult *res,
                     GTask *task)
{
    Dial3gppContext *ctx;
    GError          *error = NULL;

    ctx = (Dial3gppContext *) g_task_get_task_data (task);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    /* Go to next step */
    ctx->step++;
    dial_3gpp_context_step (task);
}

static void
dial_3gpp_context_step (GTask *task)
{
    Dial3gppContext             *ctx;
    MMBroadbandBearerSierraXmm  *self;

    ctx = (Dial3gppContext *) g_task_get_task_data (task);
    self = (MMBroadbandBearerSierraXmm *) g_task_get_source_object (task);

    mm_dbg ("running sierra_xmm dialing step %u/%u...", ctx->step, DIAL_3GPP_CONTEXT_STEP_LAST);

    /* Check for cancellation */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    switch (ctx->step) {
    case  DIAL_3GPP_CONTEXT_STEP_FIRST:
        ctx->step++;
         /* fall down to next step */
    case DIAL_3GPP_CONTEXT_STEP_CHECK_CGACT:
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "+CGACT?",
                                       3,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       (GAsyncReadyCallback)dial_3gpp_cgact_check_ready,
                                       task);
        return;

    case DIAL_3GPP_CONTEXT_STEP_SET_WPPP: {
        gchar *command;

        command = build_auth_string (task);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       (GAsyncReadyCallback)dial_3gpp_res_ready,
                                       task);
        g_free (command);
        return;
    }

    case DIAL_3GPP_CONTEXT_STEP_SET_XDNS: {
        gchar *command;

        /* Select IP family with XDNS command */
        switch (mm_bearer_properties_get_ip_type (mm_base_bearer_peek_config (MM_BASE_BEARER (self)))) {
        case MM_BEARER_IP_FAMILY_IPV4:
            command = g_strdup_printf ("+XDNS=%u,1", ctx->cid);
            break;
        case MM_BEARER_IP_FAMILY_IPV6:
            command = g_strdup_printf ("+XDNS=%u,2", ctx->cid);
            break;
        case MM_BEARER_IP_FAMILY_IPV4V6: /* Default to IPV4V6 dynamic DNS request */
        default:
            command = g_strdup_printf ("+XDNS=%u,3", ctx->cid);
            break;
        }
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       (GAsyncReadyCallback)dial_3gpp_res_ready,
                                       task);
        g_free (command);
        return;
    }

    case DIAL_3GPP_CONTEXT_STEP_SET_CGACT: {
        gchar *command;

        command = g_strdup_printf ("+CGACT=1,%u", ctx->cid);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       30,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       (GAsyncReadyCallback)dial_3gpp_res_ready,
                                       task);
        g_free (command);
        return;
    }

    case DIAL_3GPP_CONTEXT_STEP_SET_DATA_CHAN: {
        gchar *command;

        /* Setup data channel mapping */
        /* See Intel XMM7160 AT Functional Spec for more info on this command */
        command = g_strdup_printf ("+XDATACHANNEL=1,1,\"/USBCDC/%u\",\"/USBHS/NCM/%u\",2,%u",
                                   ctx->usb_acm_config_index, ctx->usb_interface_config_index, ctx->cid);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       (GAsyncReadyCallback)dial_3gpp_res_ready,
                                       task);
        g_free (command);
        return;
    }

    case DIAL_3GPP_CONTEXT_STEP_START_DATA_CHAN: {
        gchar *command;

        command = g_strdup_printf ("+CGDATA=\"M-RAW_IP\",%u", ctx->cid);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       10,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       (GAsyncReadyCallback)dial_3gpp_res_ready,
                                       task);
        g_free (command);
        return;
    }

    case DIAL_3GPP_CONTEXT_STEP_LAST:
        g_task_return_pointer (task, g_object_ref (ctx->data), g_object_unref);
        g_object_unref (task);
        return;
    }
}

static void
dial_3gpp (MMBroadbandBearer *self,
           MMBaseModem *modem,
           MMPortSerialAt *primary,
           guint cid,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    GTask               *task;
    Dial3gppContext     *ctx;
    GError              *error = NULL;

    g_assert (primary != NULL);

    /* Setup task */
    task = g_task_new (self, cancellable, callback, user_data);
    ctx = g_slice_new0 (Dial3gppContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) dial_3gpp_context_free);

    /* Setup Context */
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;
    ctx->step = DIAL_3GPP_CONTEXT_STEP_FIRST;
    ctx->usb_interface_config_index = -1;
    ctx->usb_acm_config_index = -1;

    /* Get a net port for the connection */
    ctx->data = mm_base_modem_peek_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!ctx->data) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "No valid data port found to launch connection");
        g_object_unref (task);
        return;
    }
    g_object_ref (ctx->data);

    ctx->secondary = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (modem));
    if (ctx->secondary)
        g_object_ref (ctx->secondary);

    /* Validate the USB configuration */
    ctx->usb_interface_config_index = mm_kernel_device_get_property_as_int(mm_port_peek_kernel_device(ctx->data), "ID_MM_XMM_NCM_ID");
    ctx->usb_acm_config_index = mm_kernel_device_get_property_as_int(mm_port_peek_kernel_device(ctx->data), "ID_MM_XMM_ACM_ID");

    if (ctx->usb_interface_config_index < 0 || ctx->usb_acm_config_index < 0) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Run! */
    dial_3gpp_context_step (task);
}

/*****************************************************************************/
/* 3GPP IP config retrieval (sub-step of the 3GPP Connection sequence) */

typedef enum {
    GET_IP_CONFIG_3GPP_CONTEXT_STEP_FIRST = 0,
    GET_IP_CONFIG_3GPP_CONTEXT_STEP_CGPADDR,
    GET_IP_CONFIG_3GPP_CONTEXT_STEP_XDNS,
    GET_IP_CONFIG_3GPP_CONTEXT_STEP_LAST
} GetIPConfig3gppContextStep;

typedef struct {
    MMBaseModem                 *modem;
    MMPortSerialAt              *primary;
    guint                       cid;
    MMBearerIpFamily            ip_family;
    MMPort                      *data;
    GetIPConfig3gppContextStep  step;
    MMBearerIpConfig            *ipv4_config;
    MMBearerIpConfig            *ipv6_config;
} GetIPConfig3gppContext;

static void
get_ip_config_3gpp_context_free (GetIPConfig3gppContext *ctx)
{
    g_clear_object (&ctx->modem);
    g_clear_object (&ctx->primary);
    g_clear_object (&ctx->data);
    g_clear_object (&ctx->ipv4_config);
    g_clear_object (&ctx->ipv6_config);
}

/* forward declaration */
static void get_ip_config_3gpp_context_step (GTask *task);

static gboolean
get_ip_config_3gpp_finish (MMBroadbandBearer *self,
                           GAsyncResult *res,
                           MMBearerIpConfig **ipv4_config,
                           MMBearerIpConfig **ipv6_config,
                           GError **error)
{
    MMBearerConnectResult *configs;
    MMBearerIpConfig *ipv4;
    MMBearerIpConfig *ipv6;

    configs = g_task_propagate_pointer (G_TASK (res), error);
    if (!configs)
        return FALSE;

    /* IPv4 config */
    ipv4 = mm_bearer_connect_result_peek_ipv4_config (configs);
    if (ipv4)
        *ipv4_config = g_object_ref (ipv4);

    /* IPv6 config */
    ipv6 = mm_bearer_connect_result_peek_ipv6_config (configs);

    if (ipv6)
        *ipv6_config = g_object_ref (ipv6);

    mm_bearer_connect_result_unref (configs);
    return TRUE;
}

static void
ip_info_ready (MMBaseModem *modem,
               GAsyncResult *res,
               GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    guint cid;
    gchar *ipv4addr = NULL;
    gchar *ipv6addr = NULL;
    GetIPConfig3gppContext *ctx;

    ctx = (GetIPConfig3gppContext *) g_task_get_task_data (task);

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Parse response */
    if (!mm_xmm_parse_cgpaddr_response (response, &cid, &ipv4addr, &ipv6addr, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_warn_if_fail (cid == ctx->cid);
    /* Create IPv4 config */
    if (ipv4addr) {
        ctx->ipv4_config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (ctx->ipv4_config, MM_BEARER_IP_METHOD_STATIC);
        mm_bearer_ip_config_set_address (ctx->ipv4_config, ipv4addr);
        mm_bearer_ip_config_set_prefix (ctx->ipv4_config, 0);
        g_free (ipv4addr);
    }
    /* IPv6 config */
    if (ipv6addr) {
        ctx->ipv6_config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (ctx->ipv6_config, MM_BEARER_IP_METHOD_STATIC);
        mm_bearer_ip_config_set_address (ctx->ipv6_config, ipv6addr);
        mm_bearer_ip_config_set_prefix (ctx->ipv6_config, 0);
        g_free (ipv6addr);
    }

    ctx->step++;
    get_ip_config_3gpp_context_step (task);
}

static void
dns_info_ready (MMBaseModem *modem,
                GAsyncResult *res,
                GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    GStrv dns_v4 = NULL;
    GStrv dns_v6 = NULL;
    GetIPConfig3gppContext *ctx;

    ctx = (GetIPConfig3gppContext *) g_task_get_task_data (task);

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Parse XDNS results and return two vectors containing IPV4 and IPV6 addresses */
    if (!mm_xmm_parse_xdns_query_response (response, ctx->cid, &dns_v4, &dns_v6, &error)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                    "Error parsing +XDNS response: '%s'",
                                    response);
        g_object_unref (task);
        return;
    }
    if (ctx->ipv4_config)
        mm_bearer_ip_config_set_dns (ctx->ipv4_config, (const gchar **)dns_v4);

    if (ctx->ipv6_config)
        mm_bearer_ip_config_set_dns (ctx->ipv6_config, (const gchar **)dns_v6);

    ctx->step++;
    get_ip_config_3gpp_context_step (task);
}

static void
get_ip_config_3gpp_context_step (GTask *task)
{
    GetIPConfig3gppContext *ctx;

    ctx = (GetIPConfig3gppContext *) g_task_get_task_data (task);

    mm_dbg ("running sierra_xmm get ip config step %u/%u...", ctx->step, GET_IP_CONFIG_3GPP_CONTEXT_STEP_LAST);

    switch (ctx->step) {
    case GET_IP_CONFIG_3GPP_CONTEXT_STEP_FIRST:
        ctx->step++;
        /* fall down to next step */
    case GET_IP_CONFIG_3GPP_CONTEXT_STEP_CGPADDR: {
        gchar *command;
        command = g_strdup_printf ("+CGPADDR=%u", ctx->cid);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       (GAsyncReadyCallback)ip_info_ready,
                                       task);
        g_free (command);
        return;
    }

    case GET_IP_CONFIG_3GPP_CONTEXT_STEP_XDNS:
        /* query DNS addresses */
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "+XDNS?",
                                       3,
                                       FALSE,
                                       FALSE,
                                       FALSE,
                                       (GAsyncReadyCallback)dns_info_ready,
                                       task);
        return;

    case GET_IP_CONFIG_3GPP_CONTEXT_STEP_LAST:
        g_task_return_pointer (task,
                           mm_bearer_connect_result_new (ctx->data, ctx->ipv4_config, ctx->ipv6_config),
                           (GDestroyNotify) mm_bearer_connect_result_unref);

        g_object_unref (task);
        return;
    }
}

static void
get_ip_config_3gpp (MMBroadbandBearer *self,
                    MMBroadbandModem *modem,
                    MMPortSerialAt *primary,
                    MMPortSerialAt *secondary,
                    MMPort *data,
                    guint cid,
                    MMBearerIpFamily ip_family,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GTask                   *task;
    GetIPConfig3gppContext  *ctx;

    g_assert (primary != NULL);
    g_assert (data != NULL);
    g_assert (modem != NULL);

    /* Setup task and create disconnect context */
    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (GetIPConfig3gppContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) get_ip_config_3gpp_context_free);

    /* Setup context */
    ctx->modem   = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid     = cid;
    ctx->ip_family = ip_family;
    ctx->data    = g_object_ref (data);
    ctx->step    = GET_IP_CONFIG_3GPP_CONTEXT_STEP_FIRST;
    ctx->ipv4_config = NULL;
    ctx->ipv6_config = NULL;

    /* Start */
    get_ip_config_3gpp_context_step (task);
}

/*****************************************************************************/
/* Disconnect 3GPP */

typedef enum {
    DISCONNECT_3GPP_CONTEXT_STEP_FIRST = 0,
    DISCONNECT_3GPP_CONTEXT_STEP_CGACT,
    DISCONNECT_3GPP_CONTEXT_STEP_LAST
} Disconnect3gppContextStep;

typedef struct {
    MMBaseModem                *modem;
    MMPortSerialAt             *primary;
    MMPort                     *data;
    guint                       cid;
    Disconnect3gppContextStep   step;
} Disconnect3gppContext;

static void
disconnect_3gpp_context_free (Disconnect3gppContext *ctx)
{
    g_clear_object (&ctx->modem);
    g_clear_object (&ctx->primary);
    g_clear_object (&ctx->data);
    g_slice_free (Disconnect3gppContext, ctx);
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer  *self,
                        GAsyncResult       *res,
                        GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

/* forward declaration */
static void
disconnect_3gpp_context_step (GTask *task);

static void
cgact_disconnect_ready (MMBaseModem  *modem,
                        GAsyncResult *res,
                        GTask        *task)
{
    Disconnect3gppContext *ctx;
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (modem, res, &error);

    if (error) {
        if(!g_error_matches (error,
                            MM_CONNECTION_ERROR,
                            MM_CONNECTION_ERROR_NO_CARRIER))
        {
        mm_dbg ("PDP context deactivation failed (not fatal): %s", error->message);
        }
        g_error_free (error);
    }

    ctx = (Disconnect3gppContext *) g_task_get_task_data (task);

    /* Go on to next step */
    ctx->step++;
    disconnect_3gpp_context_step (task);
}

static void
disconnect_3gpp_context_step (GTask *task)
{
    Disconnect3gppContext *ctx;

    ctx = (Disconnect3gppContext *) g_task_get_task_data (task);

    mm_dbg ("running sierra_xmm disconnect step %u/%u...", ctx->step, DISCONNECT_3GPP_CONTEXT_STEP_LAST);

    switch (ctx->step) {
    case DISCONNECT_3GPP_CONTEXT_STEP_FIRST:
        ctx->step++;
        /* fall down to next step */
    case DISCONNECT_3GPP_CONTEXT_STEP_CGACT: {
        gchar *command;

        command = g_strdup_printf ("+CGACT=0,%u", ctx->cid );
        mm_base_modem_at_command_full (ctx->modem,
                                    ctx->primary,
                                    command,
                                    30,
                                    FALSE,
                                    FALSE,
                                    FALSE,
                                    (GAsyncReadyCallback) cgact_disconnect_ready,
                                    task);
        g_free (command);
        return;
    }

    case DISCONNECT_3GPP_CONTEXT_STEP_LAST:{
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    }
}

static void
disconnect_3gpp (MMBroadbandBearer  *self,
                 MMBroadbandModem   *modem,
                 MMPortSerialAt     *primary,
                 MMPortSerialAt     *secondary,
                 MMPort             *data,
                 guint               cid,
                 GAsyncReadyCallback callback,
                 gpointer            user_data)
{
    GTask                 *task;
    Disconnect3gppContext *ctx;

    g_assert (primary != NULL);
    g_assert (data != NULL);
    g_assert (modem != NULL);

    /* Setup task and create disconnect context */
    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (Disconnect3gppContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) disconnect_3gpp_context_free);

    /* Setup context */
    ctx->modem   = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->data    = g_object_ref (data);
    ctx->cid     = cid;
    ctx->step    = DISCONNECT_3GPP_CONTEXT_STEP_FIRST;

    /* Start */
    disconnect_3gpp_context_step (task);
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_sierra_xmm_new_finish (GAsyncResult *res,
                                         GError **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}

void
mm_broadband_bearer_sierra_xmm_new (MMBroadbandModemSierraXmm *modem,
                                    MMBearerProperties *config,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_SIERRA_XMM,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_sierra_xmm_init (MMBroadbandBearerSierraXmm *self)
{
}

static void
mm_broadband_bearer_sierra_xmm_class_init (MMBroadbandBearerSierraXmmClass *klass)
{
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);
    // MMBaseBearerClass      *base_bearer_class      = MM_BASE_BEARER_CLASS (klass);

    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;

    broadband_bearer_class->get_ip_config_3gpp = get_ip_config_3gpp;
    broadband_bearer_class->get_ip_config_3gpp_finish = get_ip_config_3gpp_finish;

    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
