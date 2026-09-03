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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../core/sr_module.h"
#include "../../core/pvar.h"
#include "../../core/mod_fix.h"
#include "../../core/parser/msg_parser.h"
#include "../../core/str.h"

#include "kazoo_params.h"
#include "kazoo_api.h"




int kz_amqp_add_connection_k(modparam_t type, void* val)
{
	return kz_amqp_add_connection((char*) val);
}

int kz_amqp_declare_exchange_k(struct sip_msg* msg, char* payload)
{
	str payload_s = STR_NULL;

	if (fixup_get_svalue(msg, (gparam_p)payload, &payload_s) != 0) {
		LM_ERR("cannot get payload value\n");
		return -1;
	}
	return kz_amqp_declare_exchange(payload_s.s);
}


int kz_amqp_subscribe_k(struct sip_msg* msg, char* payload)
{
	str payload_s = STR_NULL;

	if (fixup_get_svalue(msg, (gparam_p)payload, &payload_s) != 0) {
		LM_ERR("cannot get payload value\n");
		return -1;
	}
	return kz_amqp_subscribe(payload_s.s);
}

int kz_amqp_subscribe_simple_k(struct sip_msg* msg, char* str_exchange, char* str_exchange_type, char* str_queue, char* str_routing_key)
{
	return kz_amqp_subscribe_simple(str_exchange, str_exchange_type, str_queue, str_routing_key);
}


