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

#ifndef KZ_AMQP_DEFS_H_
#define KZ_AMQP_DEFS_H_

#define MAX_ROUTING_KEY_SIZE 255

typedef enum {
	KZ_AMQP_CMD_PUBLISH     = 1,
	KZ_AMQP_CMD_CALL    = 2,
	KZ_AMQP_CMD_CONSUME = 3,
	KZ_AMQP_CMD_ACK = 4,
	KZ_AMQP_CMD_ASYNC_CALL    = 5,
	KZ_AMQP_CMD_RELAY    = 6,
	KZ_AMQP_CMD_STOP_RELAY    = 7,
} kz_amqp_pipe_cmd_type;


typedef struct kz_amqp_listener_t kz_amqp_listener;
typedef kz_amqp_listener *kz_amqp_listener_ptr;

struct kz_amqp_listener_t {

	char * name;

	int connections;
	int federated_connections;
	int channels;
	int federated_channels;
    int consistent_worker;
    char *consistent_worker_key;
    char* eventkey;
    char* eventsubkey;
    char* route;

	kz_amqp_listener_ptr next;
};

typedef struct kz_amqp_listeners_t kz_amqp_listeners;
typedef kz_amqp_listeners *kz_amqp_listeners_ptr;
struct kz_amqp_listeners_t
{
	kz_amqp_listener_ptr head;
	kz_amqp_listener_ptr tail;
};


typedef struct kz_amqp_timer_t {
	struct event *ev;
	struct itimerspec *timer;
	int    fd;
} kz_amqp_timer, *kz_amqp_timer_ptr;




typedef struct {
    gen_lock_t lock;
	kz_amqp_pipe_cmd_type type;
	char* exchange;
	char* exchange_type;
	char* routing_key;
	char* reply_routing_key;
	char* queue;
	char* payload;
	char* return_payload;
	char* ci;
	int   return_code;
	int   consumer;
	int   server_id;
	int   flags;
	uint64_t delivery_tag;
	uint16_t channel;
	struct timeval timeout;

	/* async */
	char *cb_route;
	char *err_route;
	unsigned int t_hash;
	unsigned int t_label;
	unsigned int t_reply_route;
	unsigned int t_failure_route;

	/* args */
	char* arguments;

	int relay_connection_id;


} kz_amqp_cmd, *kz_amqp_cmd_ptr;

typedef struct {
	str* message_id;

	/* timer */
	struct event *timer_ev;
	int timerfd;

} kz_amqp_cmd_timeout, *kz_amqp_cmd_timeout_ptr;

typedef struct kz_amqp_cmd_entry_t {
	kz_amqp_cmd_ptr cmd;
	struct kz_amqp_cmd_entry_t* next;
} kz_amqp_cmd_entry, *kz_amqp_cmd_entry_ptr;

typedef struct kz_amqp_cmd_table_t {
	kz_amqp_cmd_entry_ptr entries;
//	gen_lock_t lock;
} kz_amqp_cmd_table, *kz_amqp_cmd_table_ptr;


typedef enum {
	KZ_AMQP_EVENT     = 1,
	KZ_AMQP_CALL    = 2,
	KZ_AMQP_TRANSACTION = 3,
	KZ_AMQP_RELAY = 4,
} kz_amqp_delivery_type;

typedef struct {
	char* payload;
	uint64_t delivery_tag;
	uint16_t channel;
	char* event_key;
	char* event_subkey;
	char* message_id;
	char* routing_key;
	char* event_category;
	char* event_name;
	char* ci;

	unsigned int t_hash;
	unsigned int t_label;
	unsigned int t_reply_route;
	unsigned int t_failure_route;
	int          t_code;
	char*        t_reason;
	char*        t_body;
	char*        t_headers;
	char*        t_to_tag;
	int          no_stat;
	kz_amqp_delivery_type type;

	kz_amqp_cmd_ptr cmd;
} kz_amqp_consumer_delivery, *kz_amqp_consumer_delivery_ptr;



#endif /* KZ_AMQP_PIPE_H_ */

