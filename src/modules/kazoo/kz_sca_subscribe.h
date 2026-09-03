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
#ifndef KZ_SCA_SUBSCRIBE_H
#define KZ_SCA_SUBSCRIBE_H

#include "kz_sca.h"
#include "kz_sca_dialog.h"

enum
{
	KZ_SCA_SUBSCRIPTION_STATE_ACTIVE,
	KZ_SCA_SUBSCRIPTION_STATE_PENDING,
	KZ_SCA_SUBSCRIPTION_STATE_TERMINATED,
	KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_DEACTIVATED,
	KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_GIVEUP,
	KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_NORESOURCE,
	KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_PROBATION,
	KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_REJECTED,
	KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_TIMEOUT,
};

struct _kz_sca_subscription
{
	str subscriber; // contact: user@ip
	str target_aor; // account of record to watch: user@domain
	int event;		// "call-info", "line-seize"
	time_t expires; // expiration date of subscription
	int state;		// active, pending, terminated
	int index;		// seized appearance-index, line-seize only

	kz_sca_dialog dialog; // call-id, to- and from-tags, cseq

	str rr; // Record-Route header values

	int db_cmd_flag; // track whether to INSERT or UPDATE
	int server_id;   // server

	str endpoint_id;
	str sockinfo;
	str user_agent;
	
};
typedef struct _kz_sca_subscription kz_sca_subscription_t;

enum
{
	KZ_SCA_SUBSCRIPTION_TERMINATE_OPT_UNSUBSCRIBE = (1 << 0),
	KZ_SCA_SUBSCRIPTION_TERMINATE_OPT_RELEASE_APPEARANCE = (1 << 1),
};
#define KZ_SCA_SUBSCRIPTION_TERMINATE_OPT_DEFAULT  \
	(KZ_SCA_SUBSCRIPTION_TERMINATE_OPT_UNSUBSCRIBE \
			| KZ_SCA_SUBSCRIPTION_TERMINATE_OPT_RELEASE_APPEARANCE)

enum
{
	KZ_SCA_SUBSCRIPTION_CREATE_OPT_DEFAULT = 0,
	KZ_SCA_SUBSCRIPTION_CREATE_OPT_RAW_EXPIRES = (1 << 0),
};

extern const str KZ_SCA_METHOD_SUBSCRIBE;

#define KZ_SCA_SUBSCRIPTION_IS_TERMINATED(sub1)            \
	((sub1)->state >= KZ_SCA_SUBSCRIPTION_STATE_TERMINATED \
			&& (sub1)->state <= KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_TIMEOUT)

#define KZ_SCA_SUB_REPLY_ERROR(mod, scode, smsg, sreply) kz_sca_subscription_reply((mod), (scode), (smsg), KZ_SCA_EVENT_TYPE_CALL_INFO, -1, (sreply))

int kz_sca_handle_subscribe(sip_msg_t *);
int kz_sca_subscription_reply(kz_sca_mod *, int, char *, int, int, sip_msg_t *);

int kz_sca_subscription_from_db_result(db1_res_t *, kz_sca_subscription_t *);
int kz_sca_subscriptions_restore_from_db(kz_sca_mod *);
int kz_sca_subscription_db_update(void);
void kz_sca_subscription_db_update_timer(unsigned, void *);
void kz_sca_subscription_purge_expired(unsigned int, void *);
void kz_sca_subscription_state_to_str(int, str *);

int kz_sca_subscription_aor_has_subscribers(int, str *);
int kz_sca_subscription_delete_subscriber_for_event(kz_sca_mod *, str *, str *, str *);
int kz_sca_subscription_delete_subscriber_for_event_call_id(kz_sca_mod *scam, str *subscriber, str *event, str *aor, str *call_id);
int kz_sca_subscription_terminate(kz_sca_mod *, str *, int, str *, int, int);
int ki_kz_sca_handle_subscribe(sip_msg_t *msg, str *, str *);

#endif // KZ_SCA_SUBSCRIBE_H
