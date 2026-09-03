/*
 * $Id$
 *
 * Kazoo module interface
 *
 * Copyright (C) 2010-2014 2600Hz
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * Kamailio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * Kamailio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * --------
 * 2014-08  first version (2600hz)
 */
#ifndef KZ_AMQP_UTIL_H
#define KZ_AMQP_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include "../../core/mem/mem.h"
#include "../../core/pvar.h"
#include "../../core/mod_fix.h"
#include "../../core/lvalue.h"
#include "../../core/route.h"
#include "../../core/receive.h"
#include "../../core/action.h"
#include "../../core/script_cb.h"


int set_non_blocking(int fd);

void kz_amqp_util_encode(const str * key, char *pdest);
int kz_amqp_encode_ex(str* unencoded, pv_value_p dst_val);
int kz_amqp_encode(struct sip_msg* msg, char* unencoded, char* encoded);

#endif
