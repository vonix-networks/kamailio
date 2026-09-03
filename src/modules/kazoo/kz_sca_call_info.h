/*
 * Copyright (C) 2012 Andrew Mortensen
 *
 * This file is part of the sca module for Kamailio, a free SIP server.
 *
 * The sca module is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * The sca module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA. 02110-1301 USA
 */
#ifndef KZ_SCA_CALL_INFO_H
#define KZ_SCA_CALL_INFO_H

#include "kz_sca.h"
#include "kz_sca_subscribe.h"

// pass to kz_sca_notify_subscriber to include all appearances in Call-Info hdr
#define KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY 0

enum
{
	KZ_SCA_CALL_INFO_SHARED_NONE = 0,
	KZ_SCA_CALL_INFO_SHARED_CALLER = (1 << 0),
	KZ_SCA_CALL_INFO_SHARED_CALLEE = (1 << 1),
};
#define KZ_SCA_CALL_INFO_SHARED_BOTH \
	(KZ_SCA_CALL_INFO_SHARED_CALLER | KZ_SCA_CALL_INFO_SHARED_CALLEE)

struct _kz_sca_call_info
{
	str kz_sca_uri;
	int index;
	int state;
	str uri;
	// mask tracking which endpoints in a call are shared
	int ua_shared;
};
typedef struct _kz_sca_call_info kz_sca_call_info;

#define KZ_SCA_CALL_INFO_EMPTY(ci1)                                   \
	((void *)(ci1) == NULL                                         \
			|| ((ci1)->index == KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY \
					   && (ci1)->state == KZ_SCA_APPEARANCE_STATE_UNKNOWN))

#define KZ_SCA_CALL_INFO_IS_SHARED_CALLER(ci1)         \
	(!KZ_SCA_CALL_INFO_EMPTY((ci1))                    \
			&& (((kz_sca_call_info *)(ci1))->ua_shared \
					   & KZ_SCA_CALL_INFO_SHARED_CALLER))

#define KZ_SCA_CALL_INFO_IS_SHARED_CALLEE(ci1)         \
	(!KZ_SCA_CALL_INFO_EMPTY((ci1))                    \
			&& (((kz_sca_call_info *)(ci1))->ua_shared \
					   & KZ_SCA_CALL_INFO_SHARED_CALLEE))

extern const str KZ_SCA_CALL_INFO_HEADER_STR;

void kz_sca_call_info_sl_reply_cb(void *);
void kz_sca_call_info_ack_cb(struct cell *, int, struct tmcb_params *);

int kz_sca_call_info_build_header(kz_sca_mod *, kz_sca_subscription_t *, char *, int);
int kz_sca_call_info_append_header_for_appearance_index(
		kz_sca_subscription_t *, int, char *, int);

int kz_sca_call_info_body_parse(str *, kz_sca_call_info *);
int kz_sca_call_info_free(kz_sca_call_info *);

#endif // KZ_SCA_CALL_INFO_H
