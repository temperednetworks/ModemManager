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
#ifndef MM_MODEM_HELPERS_INFINEON_H
#define MM_MODEM_HELPERS_INFINEON_H

#include <glib.h>

gboolean
mm_infineon_parse_cgpaddr_response (const gchar *reply,
                                    guint *cid,
                                    gchar **ipv4addr, gchar **ipv6addr,
                                    GError **error);

gboolean
mm_infineon_parse_xdns_query_response (const gchar *reply,
                                       guint cid,
                                       GStrv *dns_v4, GStrv *dns_v6,
                                       GError **error);
#endif  /* MM_MODEM_HELPERS_INFINEON_H */
