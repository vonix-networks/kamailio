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
#include <unistd.h>
#include "../../core/mem/mem.h"
#include "../../core/locking.h"
#include "../../core/mem/shm.h"

extern int log_stderr;

#include "kz_amqp_mem.h"


str* kz_str_dup(str* src)
{
	char *dst_char = (char*)shm_malloc(sizeof(str)+src->len+1);
	if (!dst_char) {
		LM_ERR("error allocating shared memory for str");
		return NULL;
	}
	str* dst = (str*)dst_char;
	dst->s = dst_char+sizeof(str);

	memcpy(dst->s, src->s, src->len);
	dst->len = src->len;
	dst->s[dst->len] = '\0';
	return dst;
}

str* kz_str_dup_from_char(char* src)
{
	int len = strlen(src);
	char *dst_char = (char*)shm_malloc(sizeof(str)+len+1);
	if (!dst_char) {
		LM_ERR("error allocating shared memory for str");
		return NULL;
	}
	str* dst = (str*)dst_char;
	dst->s = dst_char+sizeof(str);

	memcpy(dst->s, src, len);
	dst->len = len;
	dst->s[dst->len] = '\0';
	return dst;
}


char *kz_amqp_str_dup(str *src)
{
	char *res;

	if (!src || !src->s)
		return NULL;
	if (!(res = (char *) shm_malloc(src->len + 1)))
		return NULL;
	strncpy(res, src->s, src->len);
	res[src->len] = 0;
	return res;
}

char *kz_amqp_string_dup(char *src)
{
	char *res;
	int sz;
	if (!src )
		return NULL;

	sz = strlen(src);
	if (!(res = (char *) shm_malloc(sz + 1)))
		return NULL;
	strncpy(res, src, sz);
	res[sz] = 0;
	return res;
}

char *kz_amqp_local_string_dup(char *src)
{
	char *res;
	int sz;
	if (!src )
		return NULL;

	sz = strlen(src);
	if (!(res = (char *) pkg_malloc(sz + 1)))
		return NULL;
	strncpy(res, src, sz);
	res[sz] = 0;
	return res;
}

char *kz_local_amqp_str_dup(str *src)
{
	char *res;

	if (!src || !src->s)
		return NULL;
	if (!(res = (char *) pkg_malloc(src->len + 1)))
		return NULL;
	strncpy(res, src->s, src->len);
	res[src->len] = 0;
	return res;
}


str* kz_local_str_dup(str* src)
{
	char *dst_char = (char*)pkg_malloc(sizeof(str)+src->len+1);
	if (!dst_char) {
		LM_ERR("error allocating shared memory for str");
		return NULL;
	}
	str* dst = (str*)dst_char;
	dst->s = dst_char+sizeof(str);

	memcpy(dst->s, src->s, src->len);
	dst->len = src->len;
	dst->s[dst->len] = '\0';
	return dst;
}


void kz_amqp_free_pipe_cmd(kz_amqp_cmd_ptr cmd)
{
	if(cmd == NULL)
		return;
	if (cmd->exchange)
		shm_free(cmd->exchange);
	if (cmd->exchange_type)
		shm_free(cmd->exchange_type);
	if (cmd->queue)
		shm_free(cmd->queue);
	if (cmd->routing_key)
		shm_free(cmd->routing_key);
	if (cmd->reply_routing_key)
		shm_free(cmd->reply_routing_key);
	if (cmd->payload)
		shm_free(cmd->payload);
	if (cmd->return_payload)
		shm_free(cmd->return_payload);
	if (cmd->ci)
		shm_free(cmd->ci);
	if (cmd->cb_route)
		shm_free(cmd->cb_route);
	if (cmd->err_route)
		shm_free(cmd->err_route);
	if (cmd->arguments)
		shm_free(cmd->arguments);
	lock_release(&cmd->lock);
	lock_destroy(&cmd->lock);
	shm_free(cmd);
}

void kz_amqp_free_consumer_delivery(kz_amqp_consumer_delivery_ptr ptr)
{
	if(ptr == NULL)
		return;
	if(ptr->payload)
		shm_free(ptr->payload);
	if(ptr->event_key)
		shm_free(ptr->event_key);
	if(ptr->event_subkey)
		shm_free(ptr->event_subkey);
	if(ptr->message_id)
		shm_free(ptr->message_id);
	if(ptr->routing_key)
		shm_free(ptr->routing_key);
	if(ptr->event_category)
		shm_free(ptr->event_category);
	if(ptr->event_name)
		shm_free(ptr->event_name);
	if(ptr->ci)
		shm_free(ptr->ci);
	if(ptr->t_body)
		shm_free(ptr->t_body);
	if(ptr->t_headers)
		shm_free(ptr->t_headers);
	if(ptr->t_reason)
		shm_free(ptr->t_reason);
	if(ptr->t_to_tag)
		shm_free(ptr->t_to_tag);
	if(ptr->cmd)
		kz_amqp_free_pipe_cmd(ptr->cmd);
	shm_free(ptr);
}


kz_amqp_consumer_delivery_ptr kz_amqp_alloc_consumer_delivery()
{
	kz_amqp_consumer_delivery_ptr ptr = (kz_amqp_consumer_delivery_ptr) shm_malloc(sizeof(kz_amqp_consumer_delivery));
	if(ptr == NULL) {
		LM_ERR("failed to allocate kz_amqp_consumer_delivery in process %d\n", getpid());
		return NULL;
	}
	memset(ptr, 0, sizeof(kz_amqp_consumer_delivery));
	return ptr;
}

kz_amqp_cmd_ptr kz_amqp_alloc_pipe_cmd()
{
	kz_amqp_cmd_ptr cmd = (kz_amqp_cmd_ptr)shm_malloc(sizeof(kz_amqp_cmd));
	if(cmd == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd in process %d\n", getpid());
		return NULL;
	}
	memset(cmd, 0, sizeof(kz_amqp_cmd));
	if(lock_init(&cmd->lock)==NULL)  {
		LM_ERR("cannot init the lock for pipe command in process %d\n", getpid());
		lock_dealloc(&cmd->lock);
		kz_amqp_free_pipe_cmd(cmd);
		return NULL;
	}
	lock_get(&cmd->lock);
	return cmd;
}

