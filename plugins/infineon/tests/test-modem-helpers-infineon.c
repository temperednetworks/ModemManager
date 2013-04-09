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
 * Copyright (C) 2017 Tempered Networks Inc
 *
 */
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers.h"
#include "mm-modem-helpers-infineon.h"

/*****************************************************************************/
/* Test +CGPADDR responses */

typedef struct {
    const gchar *str;
    const guint cid;
    const gboolean success;
    const gchar *ipv4addr;
    const gchar *ipv6addr;
} CgpAddrResponseText;

static const CgpAddrResponseText cgpaddr_response_tests[] = {
    { .str = "+CGPADDR: 1,\"10.11.12.133\"\r\n",
      .cid = 1,
      .success = TRUE,
      .ipv4addr = "10.11.12.133",
      .ipv6addr = NULL
    },
    { .str = "+CGPADDR: 1,\"FE80:0:0:0:0:6B:B43D:6A01\"\r\n",
      .cid = 1,
      .success = TRUE,
      .ipv4addr = NULL,
      .ipv6addr = "FE80:0:0:0:0:6B:B43D:6A01"
    },
    { .str = "+CGPADDR: 1,\"0.0.0.0\"\r\n",
      .cid = 1,
      .success = FALSE,
      .ipv4addr = NULL,
      .ipv6addr = NULL
    },
    { .str = "+CGPADDR: 1,\"0:0:0:0:0:0:0:0\"\r\n",
      .cid = 1,
      .success = FALSE,
      .ipv4addr = NULL,
      .ipv6addr = NULL
    },
    { .str = "+CGPADDR: 2,\"0.0.0.0\",\"0:0:0:0:0:0:0:0\"\r\n",
      .cid = 2,
      .success = FALSE,
      .ipv4addr = NULL,
      .ipv6addr = NULL
    },
    { .str = "+CGPADDR: 3,\"100.120.138.254\",\"FE80:0:0:0:0:6B:B43E:1E01\"\r\n",
      .cid = 3,
      .success = TRUE,
      .ipv4addr = "100.120.138.254",
      .ipv6addr = "FE80:0:0:0:0:6B:B43E:1E01"
    },
    { .str = "+CGPADDR: 3,\"0.0.0.0\",\"FE80:0:0:0:0:6B:B43E:1E01\"\r\n",
      .cid = 3,
      .success = TRUE,
      .ipv4addr = NULL,
      .ipv6addr = "FE80:0:0:0:0:6B:B43E:1E01"
    },
    { .str = "+CGPADDR: 3,\"100.120.138.254\",\"0:0:0:0:0:0:0:0\"\r\n",
      .cid = 3,
      .success = TRUE,
      .ipv4addr = "100.120.138.254",
      .ipv6addr = NULL
    }
};

static void
test_mm_infineon_parse_cgpaddr_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (cgpaddr_response_tests); i++) {
        GError   *error = NULL;
        gboolean  success;
        guint     cid  = G_MAXUINT;
        gchar     *ipv4addr;
        gchar     *ipv6addr;

        success = mm_infineon_parse_cgpaddr_response (cgpaddr_response_tests[i].str,
                                                      &cid, &ipv4addr, &ipv6addr,
                                                      &error);

        if (!cgpaddr_response_tests[i].success) {
            g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
            g_assert (!success);
        } else {
            g_assert (success);
            g_assert_no_error (error);
        }

        g_assert_cmpuint (cgpaddr_response_tests[i].cid,  ==, cid);
        g_assert_cmpstr (cgpaddr_response_tests[i].ipv4addr, ==, ipv4addr);
        g_assert_cmpstr (cgpaddr_response_tests[i].ipv6addr,  ==, ipv6addr);
    }
}

/*****************************************************************************/
/* Test +XDNS parser */

typedef struct {
    const gchar *str;
    const guint  cid;
    const guint  dns_v4len;
    const gchar *dns_v4[3];
    const guint  dns_v6len;
    const gchar *dns_v6[3];
} XdnsResponseText;

static const XdnsResponseText xdns_response_tests[] = {
    { .str = "+XDNS: 1, \"172.26.38.1\", \"0.0.0.0\"\r\n\r\n+XDNS: 2, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 3, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 4, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 5, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n",
      .cid = 1,
      .dns_v4len = 1,
      .dns_v4 = { "172.26.38.1", NULL },
      .dns_v6len = 0,
      .dns_v6 = { NULL }
    },
    { .str = "+XDNS: 1, \"172.26.38.1\", \"172.26.38.2\"\r\n\r\n+XDNS: 2, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 3, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 4, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 5, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n",
      .cid = 1,
      .dns_v4len = 2,
      .dns_v4 = { "172.26.38.1", "172.26.38.2", NULL },
      .dns_v6len = 0,
      .dns_v6 = { NULL }
    },
    { .str = "+XDNS: 1, \"172.26.38.1\", \"0.0.0.0\"\r\n\r\n+XDNS: 2, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 3, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 4, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 5, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n",
      .cid = 2,
      .dns_v4len = 0,
      .dns_v4 = { NULL },
      .dns_v6len = 0,
      .dns_v6 = { NULL }
    },
    { .str = "+XDNS: 1, \"172.26.38.1\", \"0.0.0.0\"\r\n\r\n+XDNS: 2, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 3, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 4, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 5, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n",
      .cid = 0,
      .dns_v4len = 0,
      .dns_v4 = { NULL },
      .dns_v6len = 0,
      .dns_v6 = { NULL }
    },
    { .str = "+XDNS: 1, \"2001:4888:53:FF00:524:D:0:0\", \"2001:4888:52:FF00:528:D:0:0\"\r\n\r\n+XDNS: 2, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 3, \"198.224.166.135\", \"198.224.167.135\"\r\n\r\n+XDNS: 3, \"198.224.166.135\", \"2001:4888:53:FF00:524:D:0:0\"\r\n\r\n+XDNS: 3, \"198.224.166.135\", \"2001:4888:52:FF00:528:D:0:0\"\r\n\r\n+XDNS: 4, \"0.0.0.0\", \"0.0.0.0\"\r\n",
      .cid = 1,
      .dns_v4len = 0,
      .dns_v4 = { NULL },
      .dns_v6len = 2,
      .dns_v6 = { "2001:4888:53:FF00:524:D:0:0", "2001:4888:52:FF00:528:D:0:0", NULL }
    },
    { .str = "+XDNS: 1, \"2001:4888:53:FF00:524:D:0:0\", \"2001:4888:52:FF00:528:D:0:0\"\r\n\r\n+XDNS: 2, \"0.0.0.0\", \"0.0.0.0\"\r\n\r\n+XDNS: 3, \"198.224.166.135\", \"198.224.167.135\"\r\n\r\n+XDNS: 3, \"198.224.166.135\", \"2001:4888:53:FF00:524:D:0:0\"\r\n\r\n+XDNS: 3, \"198.224.166.135\", \"2001:4888:52:FF00:528:D:0:0\"\r\n\r\n+XDNS: 4, \"0.0.0.0\", \"0.0.0.0\"\r\n",
      .cid = 3,
      .dns_v4len = 2,
      .dns_v4 = { "198.224.166.135", "198.224.167.135", NULL },
      .dns_v6len = 2,
      .dns_v6 = { "2001:4888:53:FF00:524:D:0:0", "2001:4888:52:FF00:528:D:0:0", NULL }
    },
};

static void
test_mm_infineon_parse_xdns_query_response (void)
{
    guint i, j;

    for (i = 0; i < G_N_ELEMENTS (xdns_response_tests); i++) {
        GError   *error = NULL;
        gboolean  success;
        GStrv     dns_v4;
        GStrv     dns_v6;

        success = mm_infineon_parse_xdns_query_response (xdns_response_tests[i].str,
                                                         xdns_response_tests[i].cid,
                                                         &dns_v4, &dns_v6,
                                                         &error);
        g_assert_no_error (error);
        g_assert (success);

        g_assert_cmpuint (xdns_response_tests[i].dns_v4len,  ==, g_strv_length(dns_v4));
        for (j = 0; j < xdns_response_tests[i].dns_v4len; j++) {
            g_assert_cmpstr (xdns_response_tests[i].dns_v4[j], ==, dns_v4[j]);
        }

        g_assert_cmpuint (xdns_response_tests[i].dns_v6len,  ==, g_strv_length(dns_v6));
        for (j = 0; j < xdns_response_tests[i].dns_v6len; j++) {
            g_assert_cmpstr (xdns_response_tests[i].dns_v6[j], ==, dns_v6[j]);
        }
    }
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/MM/infineon/cgpaddr", test_mm_infineon_parse_cgpaddr_response);
    g_test_add_func ("/MM/infineon/xdns", test_mm_infineon_parse_xdns_query_response);
    return g_test_run ();
}
