/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_CALLBACK_INFO_H
#define MM_CALLBACK_INFO_H

#include "mm-modem.h"

typedef struct {
	GData *qdata;
	MMModem *modem;
	MMModemFn callback;
    MMModemUIntFn uint_callback;
    guint32 uint_result;
	gpointer user_data;
	GError *error;
    guint pending_id;
} MMCallbackInfo;

MMCallbackInfo *mm_callback_info_new      (MMModem *modem,
                                           MMModemFn callback,
                                           gpointer user_data);

MMCallbackInfo *mm_callback_info_uint_new (MMModem *modem,
                                           MMModemUIntFn callback,
                                           gpointer user_data);

void            mm_callback_info_schedule (MMCallbackInfo *info);
void            mm_callback_info_cancel   (MMCallbackInfo *info);

void            mm_callback_info_set_data (MMCallbackInfo *info,
                                           const char *key,
                                           gpointer data,
                                           GDestroyNotify destroy);

gpointer        mm_callback_info_get_data (MMCallbackInfo *info,
                                           const char *key);

#endif /* MM_CALLBACK_INFO_H */
