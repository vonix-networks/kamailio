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
#ifndef KZ_SCA_NOTIFY_H
#define KZ_SCA_NOTIFY_H

#include "kz_sca_subscribe.h"

extern const str KZ_SCA_METHOD_NOTIFY;

int kz_sca_notify_subscriber(kz_sca_mod *, kz_sca_subscription_t *, int);
int kz_sca_notify_call_info_subscribers(kz_sca_mod *, str *);

int kz_sca_pv_get_subs(sip_msg_t *msg, pv_param_t *param, pv_value_t *res);
int kz_sca_pv_parse_subs_name(pv_spec_p sp, str *in);

#endif // KZ_SCA_NOTIFY_H
