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
 *
 */

#ifndef MM_BROADBAND_BEARER_SIERRA_XMM_H
#define MM_BROADBAND_BEARER_SIERRA_XMM_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-sierra-xmm.h"

#define MM_TYPE_BROADBAND_BEARER_SIERRA_XMM             (mm_broadband_bearer_sierra_xmm_get_type ())
#define MM_BROADBAND_BEARER_SIERRA_XMM(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_SIERRA_XMM, MMBroadbandBearerSierraXmm))
#define MM_BROADBAND_BEARER_SIERRA_XMM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_SIERRA_XMM, MMBroadbandBearerSierraXmmClass))
#define MM_IS_BROADBAND_BEARER_SIERRA_XMM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_SIERRA_XMM))
#define MM_IS_BROADBAND_BEARER_SIERRA_XMM_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_SIERRA_XMM))
#define MM_BROADBAND_BEARER_SIERRA_XMM_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_SIERRA_XMM, MMBroadbandBearerSierraXmmClass))

typedef struct _MMBroadbandBearerSierraXmm MMBroadbandBearerSierraXmm;
typedef struct _MMBroadbandBearerSierraXmmClass MMBroadbandBearerSierraXmmClass;
typedef struct _MMBroadbandBearerSierraXmmPrivate MMBroadbandBearerSierraXmmPrivate;

struct _MMBroadbandBearerSierraXmm {
    MMBroadbandBearer parent;
    MMBroadbandBearerSierraXmmPrivate *priv;
};

struct _MMBroadbandBearerSierraXmmClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_sierra_xmm_get_type (void);

/* Default 3GPP bearer creation implementation */
void mm_broadband_bearer_sierra_xmm_new    (MMBroadbandModemSierraXmm *modem,
                                            MMBearerProperties *config,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
MMBaseBearer *mm_broadband_bearer_sierra_xmm_new_finish    (GAsyncResult *res,
                                                            GError **error);

#endif /* MM_BROADBAND_BEARER_SIERRA_XMM_H */
