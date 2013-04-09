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
 * Copyright (C) 2017 Tempered Networks Inc.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-infineon.h"

/*************************************************************************/
/* Parse +CGPADDR response */
gboolean
mm_infineon_parse_cgpaddr_response (const gchar *reply,
                                    guint  *cid,
                                    gchar  **ipv4addr,
                                    gchar  **ipv6addr,
                                    GError **error)
{
    GRegex      *r;
    GMatchInfo  *match_info;
    GError      *inner_error = NULL;
    gchar       *ip1, *ip2;

    g_assert (reply);
    g_assert (cid);
    g_assert (ipv4addr);
    g_assert (ipv6addr);

    *ipv4addr = NULL;
    *ipv6addr = NULL;

    r = g_regex_new ("\\+CGPADDR:\\s*(\\d+)\\s*,(\"([^\"]*)\"),*(\"([^\"]*)\")*",
                     G_REGEX_OPTIMIZE | G_REGEX_RAW,
                     0, NULL);
    g_assert (r != NULL);

    if (g_regex_match (r, reply, 0, &match_info)) {
        if (!mm_get_uint_from_match_info (match_info, 1, cid)) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't parse CID from +CGPADDR response");
            goto out;
        }

        ip1 = mm_get_string_unquoted_from_match_info (match_info, 3);
        ip2 = mm_get_string_unquoted_from_match_info (match_info, 5);

        /* ip1 could contain an IPv4 or IPv6 address */
        if (ip1) {
            /* Check if IPv6 address */
            if (g_strrstr (ip1, ":")) {
                if (0 == g_strcmp0("0:0:0:0:0:0:0:0", ip1)) {
                    g_free (ip1);
                } else {
                    *ipv6addr = ip1;
                }
            /* IPv4 address */
            } else {
                if (0 == g_strcmp0("0.0.0.0", ip1)) {
                    g_free (ip1);
                } else {
                    *ipv4addr = ip1;
                }
            }
        }

        /* ip2 will always be an IPv6 address */
        if (ip2) {
            if (g_strrstr (ip2, ":") && 0 != g_strcmp0("0:0:0:0:0:0:0:0", ip2)) {
                *ipv6addr = ip2;
            } else {
                g_free (ip2);
            }
        }
    }

    if (!*ipv6addr && !*ipv4addr) {
        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "Couldn't parse a valid IP address from +CGPADDR response.");
    }

out:
    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

static gboolean
g_ptr_array_contains_string (GPtrArray *arr,
                             gchar *str)
{
    gboolean found = FALSE;
    guint    i;

    /* Check inputs */
    g_assert (arr);
    g_assert (str);

    for (i = 0; i < arr->len; i++) {
        if (0 == g_strcmp0 ((gchar *) g_ptr_array_index (arr, i), str)) {
            found = TRUE;
            break;
        }
    }
    return found;
}

/*************************************************************************/
/* Parse +XDNS response */
gboolean
mm_infineon_parse_xdns_query_response (const gchar *reply,
                                       guint  cid,
                                       GStrv  *dns_v4,
                                       GStrv  *dns_v6,
                                       GError **error)
{
    GRegex      *r;
    GMatchInfo  *match_info;
    guint       this_cid = 0;
    GPtrArray   *ipv4arr, *ipv6arr;
    gchar       *dns1, *dns2;

    ipv4arr = g_ptr_array_sized_new (3);
    ipv6arr = g_ptr_array_sized_new (3);

    r = g_regex_new ("\\+XDNS:\\s*(\\d+)\\s*,\\s*\"(.*)\"\\s*,\\s*\"(.*)\"",
                     G_REGEX_OPTIMIZE | G_REGEX_RAW | G_REGEX_MULTILINE | G_REGEX_MATCH_NEWLINE_ANY,
                     0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, reply, 0, &match_info);
    while (g_match_info_matches (match_info)) {
        if (mm_get_uint_from_match_info (match_info, 1, &this_cid) &&
            (this_cid == cid) &&
            (dns1 = mm_get_string_unquoted_from_match_info (match_info, 2)) != NULL &&
            (dns2 = mm_get_string_unquoted_from_match_info (match_info, 3)) != NULL) {
            /* Check if dns1 is an IPv6 address */
            if (g_strrstr (dns1, ":")) {
                if(!g_ptr_array_contains_string(ipv6arr, dns1))
                    g_ptr_array_add(ipv6arr, dns1);
                else
                    g_free (dns1);
            /* Otherwise dns1 is an ipv4 address. Check if valid and unique */
            } else if (0 != g_strcmp0 ("0.0.0.0", dns1) && !g_ptr_array_contains_string (ipv4arr, dns1)) {
                g_ptr_array_add (ipv4arr, dns1);
            } else {
                g_free (dns1);
            }

            /* Check if dns2 is an IPv6 address */
            if (g_strrstr (dns2, ":")) {
                if(!g_ptr_array_contains_string (ipv6arr, dns2))
                    g_ptr_array_add (ipv6arr, dns2);
                else
                    g_free (dns2);
            /* Otherwise dns2 is an ipv4 address. Check if valid and unique */
            } else if (0 != g_strcmp0 ("0.0.0.0", dns2) && !g_ptr_array_contains_string (ipv4arr, dns2)) {
                g_ptr_array_add (ipv4arr, dns2);
            } else {
                g_free (dns2);
            }
        }
        g_match_info_next (match_info, NULL);
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    /* Add Null terminators to convert from GPtrArray to GStrv.  */
    g_ptr_array_add (ipv4arr, NULL);
    g_ptr_array_add (ipv6arr, NULL);

    *dns_v4 = (GStrv) g_ptr_array_free (ipv4arr, FALSE);
    *dns_v6 = (GStrv) g_ptr_array_free (ipv6arr, FALSE);

    return TRUE;
}