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
#ifndef KZ_SCA_REPLY_H
#define KZ_SCA_REPLY_H

#include "kz_sca_common.h"
#include "kz_sca.h"

#define KZ_SCA_REPLY_ERROR(mod, scode, smsg, sreply) \
	kz_sca_reply((mod), (scode), (smsg), NULL, (sreply))

int kz_sca_reply(kz_sca_mod *, int, char *, str *, sip_msg_t *);

#endif // KZ_SCA_REPLY_H
