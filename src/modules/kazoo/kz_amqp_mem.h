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
#ifndef __KZ_AMQP_MEM_
#define __KZ_AMQP_MEM_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
//#include "../../core/locking.h"
//#include "../../core/mem/shm.h"

#include "kz_amqp_defs.h"

str* kz_str_dup(str* src);
str* kz_str_dup_from_char(char* src);
char *kz_amqp_str_dup(str *src);
char *kz_amqp_string_dup(char *src);
char *kz_local_amqp_str_dup(str *src);
char *kz_amqp_local_string_dup(char *src);
str* kz_local_str_dup(str* src);


kz_amqp_cmd_ptr kz_amqp_alloc_pipe_cmd();
kz_amqp_consumer_delivery_ptr kz_amqp_alloc_consumer_delivery();

void kz_amqp_free_pipe_cmd(kz_amqp_cmd_ptr cmd);
void kz_amqp_free_consumer_delivery(kz_amqp_consumer_delivery_ptr ptr);

#ifdef __cplusplus
}
#endif

#endif
