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
#include "kz_sca_common.h"

#include <assert.h>
#include <errno.h>

#include "kz_sca.h"
#include "kz_sca_appearance.h"
#include "kz_sca_call_info.h"
#include "kz_sca_event.h"
#include "kz_sca_notify.h"
#include "kz_sca_reply.h"
#include "kz_sca_subscribe.h"
#include "kz_sca_util.h"
#include "kz_sca_params.h"

#include "../../modules/tm/tm_load.h"

#define KZ_SCA_MAX_UA_SIZE 255

extern int errno;

static int kz_sca_subscription_copy_subscription_key(kz_sca_subscription_t *, str *);
int kz_sca_subscription_save_unsafe(kz_sca_mod *, kz_sca_subscription_t *, int, int);
void kz_sca_subscription_print(void *);

const str KZ_SCA_METHOD_SUBSCRIBE = STR_STATIC_INIT("SUBSCRIBE");

struct kz_sca_sub_state_table
{
	int state;
	char *state_name;
} state_table[] = {
		{KZ_SCA_SUBSCRIPTION_STATE_ACTIVE, "active"},
		{KZ_SCA_SUBSCRIPTION_STATE_PENDING, "pending"},
		{KZ_SCA_SUBSCRIPTION_STATE_TERMINATED, "terminated"},
		{KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_DEACTIVATED, "terminated;reason=deactivated"},
		{KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_GIVEUP, "terminated;reason=giveup"},
		{KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_NORESOURCE, "terminated;reason=noresource"},
		{KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_PROBATION, "terminated;reason=probation"},
		{KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_REJECTED, "terminated;reason=rejected"},
		{KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_TIMEOUT, "terminated;reason=timeout"},
		{-1, NULL},
};

void kz_sca_subscription_state_to_str(int state, str *state_str_out)
{
	assert(state >= 0);
	assert(state < (sizeof(state_table) / sizeof(state_table[0])));
	assert(state_str_out != NULL);

	state_str_out->len = strlen(state_table[state].state_name);
	state_str_out->s = state_table[state].state_name;
}

void kz_sca_subscription_purge_expired(unsigned int ticks, void *param)
{
	kz_sca_mod *scam = (kz_sca_mod *)param;
	kz_sca_hash_table *ht;
	kz_sca_hash_entry *ent, *ent_tmp;
	kz_sca_subscription_t *sub;
	time_t now = time(NULL);
	int state;
	int i;

	assert(scam != NULL);
	assert(scam->subscriptions != NULL);

	SCA_DBG("KZ_SCA: purging expired subscriptions\n");

	ht = scam->subscriptions;
	for(i = 0; i < ht->size; i++) {
		kz_sca_hash_table_lock_index(ht, i);

		for(ent = ht->slots[i].entries; ent != NULL; ent = ent_tmp) {
			ent_tmp = ent->next;

			sub = (kz_sca_subscription_t *)ent->value;
			if(sub == NULL || sub->expires > now) {
				continue;
			}

			if(!KZ_SCA_SUBSCRIPTION_IS_TERMINATED(sub)) {
				sub->state = KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_TIMEOUT;
				sub->expires = 0;
				sub->dialog.notify_cseq += 1;

				if(kz_sca_notify_subscriber(scam, sub, sub->index) < 0) {
					SCA_DBG("Failed to send subscription expired NOTIFY %s subscriber %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));

					// remove from subscribers list anyway
				}
				if(sub->event == KZ_SCA_EVENT_TYPE_LINE_SEIZE) {
					// only notify if the line is just seized
					state = kz_sca_appearance_state_for_index(sca, &sub->target_aor, sub->index);
					if(state == KZ_SCA_APPEARANCE_STATE_SEIZED) {
						if(kz_sca_appearance_release_index(sca, &sub->target_aor, sub->index) < 0) {
							SCA_DBG("Failed to release seized %.*s appearance-index %d\n", STR_FMT(&sub->target_aor), sub->index);
						}

						if(kz_sca_notify_call_info_subscribers(sca, &sub->target_aor) < 0) {
							SCA_DBG("KZ_SCA %s NOTIFY to all %.*s subscribers failed\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->target_aor));
							// fall through anyway. the state should propagate
							// to subscribers when they renew call-info.
						}
					}
				}
			}


			// XXX should be in a separate subscription deletion routine.
			// will need to detect whether subscriber has active appearances,
			// send notifies to others in group if necessary.
			SCA_DBG("%s subscription from %.*s expired, deleting\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));

			kz_sca_hash_table_slot_unlink_entry_unsafe(&ht->slots[i], ent);
			kz_sca_hash_entry_free(ent);
		}

		kz_sca_hash_table_unlock_index(ht, i);
	}
}

int kz_sca_subscription_from_db_row_values(db_val_t *values, kz_sca_subscription_t *sub)
{
	assert(values != NULL);
	assert(sub != NULL);

	// XXX condense to loop with preprocessor macros when there's time
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_SUBSCRIBER_COL, values, &sub->subscriber);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_AOR_COL, values, &sub->target_aor);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_EVENT_COL, values, &sub->event);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_EXPIRES_COL, values, &sub->expires);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_STATE_COL, values, &sub->state);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_APP_IDX_COL, values, &sub->index);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_CALL_ID_COL, values, &sub->dialog.call_id);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_FROM_TAG_COL, values, &sub->dialog.from_tag);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_TO_TAG_COL, values, &sub->dialog.to_tag);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_RECORD_ROUTE_COL, values, &sub->rr);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_NOTIFY_CSEQ_COL, values, &sub->dialog.notify_cseq);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_SUBSCRIBE_CSEQ_COL,values, &sub->dialog.subscribe_cseq);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_SERVER_ID_COL, values, &sub->server_id);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_ENDPOINT_ID_COL, values, &sub->endpoint_id);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_SOCKET_INFO_COL, values, &sub->sockinfo);
	kz_sca_db_subscriptions_get_value_for_column(KZ_SCA_DB_SUBS_USER_AGENT_COL, values, &sub->user_agent);

	return (0);
}

int kz_sca_subscription_to_db_row_values(kz_sca_subscription_t *sub, db_val_t *values)
{
	int notify_cseq, subscribe_cseq;

	assert(sub != NULL);
	assert(values != NULL);

	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_SUBSCRIBER_COL, values, &sub->subscriber);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_AOR_COL, values, &sub->target_aor);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_EVENT_COL, values, &sub->event);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_EXPIRES_COL, values, &sub->expires);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_STATE_COL, values, &sub->state);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_APP_IDX_COL, values, &sub->index);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_CALL_ID_COL, values, &sub->dialog.call_id);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_FROM_TAG_COL, values, &sub->dialog.from_tag);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_TO_TAG_COL, values, &sub->dialog.to_tag);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_RECORD_ROUTE_COL, values, &sub->rr);

	notify_cseq = sub->dialog.notify_cseq + 1;
	subscribe_cseq = sub->dialog.subscribe_cseq + 1;
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_NOTIFY_CSEQ_COL, values, &notify_cseq);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_SUBSCRIBE_CSEQ_COL, values, &subscribe_cseq);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_SERVER_ID_COL, values, &sub->server_id);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_ENDPOINT_ID_COL, values, &sub->endpoint_id);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_SOCKET_INFO_COL, values, &sub->sockinfo);
	kz_sca_db_subscriptions_set_value_for_column(KZ_SCA_DB_SUBS_USER_AGENT_COL, values, &sub->user_agent);

	return (0);
}

int kz_sca_subscriptions_restore_from_db(kz_sca_mod *scam)
{
	db1_con_t *db_con;
	db_key_t query_columns[1];
	db_val_t query_values[1];
	db_op_t query_ops[1];
	db_key_t result_columns[KZ_SCA_DB_SUBSCRIPTIONS_NUM_COLUMNS];
	db1_res_t *result = NULL;
	db_row_t *rows = NULL;
	db_val_t *row_values = NULL;
	kz_sca_subscription_t sub;
	str sub_key = STR_NULL;
	str **column_names;
	int num_rows;
	int i;
	int idx;
	int q_count = 0;
	int rc = -1;
	time_t now = time(NULL);

	if (!scam->db_persistance) return 0;

	db_con = scam->db_api->init(scam->cfg->db_url);
	if(db_con == NULL) {
		LM_ERR("kz_sca_subscriptions_restore_from_db: failed to connect to DB %.*s\n", STR_FMT(scam->cfg->db_url));
		return (-1);
	}

	scam->db_api->use_table(db_con, scam->cfg->subs_table);

	column_names = kz_sca_db_subscriptions_columns();
	if(column_names == NULL) {
		LM_ERR("kz_sca_subscriptions_restore_from_db: failed to get column names for KZ_SCA subscriptions table\n");
		goto done;
	}

	for(i = 0; i < KZ_SCA_DB_SUBSCRIPTIONS_NUM_COLUMNS; i++) {
		result_columns[i] = column_names[i];
	}

	query_columns[q_count] = (str *)&KZ_SCA_DB_SERVER_ID_COL_NAME;
	query_ops[q_count] = OP_EQ;
	KZ_SCA_DB_BIND_INT_VALUE(server_id, &KZ_SCA_DB_SERVER_ID_COL_NAME, query_columns,
			query_values, q_count);

	rc = db_fetch_query(scam->db_api, KZ_SCA_DB_DEFAULT_FETCH_ROW_COUNT, db_con, query_columns, query_ops, query_values, result_columns, q_count, KZ_SCA_DB_SUBSCRIPTIONS_NUM_COLUMNS, 0, &result);
	switch(rc) {
		default:
		case -1:
			LM_ERR("kz_sca_subscriptions_restore_from_db: query failed\n");
			goto done;

		case 0:
			LM_WARN("kz_sca_subscriptions_restore_from_db: DB module does not support fetch, query returning all values...\n");
			// fall through

		case 1:
			break;
	}

	do {
		rows = RES_ROWS(result);
		num_rows = RES_ROW_N(result);

		for(i = 0; i < num_rows; i++) {
			memset(&sub, 0, sizeof(kz_sca_subscription_t));

			row_values = ROW_VALUES(rows + i);

			sub.expires = row_values[KZ_SCA_DB_SUBS_EXPIRES_COL].val.time_val;
			if(sub.expires < now) {
				continue;
			}

			if(kz_sca_subscription_from_db_row_values(row_values, &sub) < 0) {
				LM_ERR("kz_sca_subscriptions_restore_from_db: skipping bad result at index %d\n", i);
				continue;
			}

			if(kz_sca_subscription_copy_subscription_key(&sub, &sub_key) < 0) {
				LM_ERR("kz_sca_subscriptions_restore_from_db: failed to copy subscription key %.*s%s\n", STR_FMT(&sub.subscriber), kz_sca_event_name_from_type(sub.event));
				continue;
			}

			idx = kz_sca_hash_table_index_for_key(sca->subscriptions, &sub_key);
			pkg_free(sub_key.s);

			kz_sca_hash_table_lock_index(sca->subscriptions, idx);

			if(kz_sca_subscription_save_unsafe(scam, &sub, idx, KZ_SCA_SUBSCRIPTION_CREATE_OPT_RAW_EXPIRES) < 0) {
				LM_ERR("kz_sca_subscriptions_restore_from_db: failed to restore %s subscription from %.*s to the hash table\n", kz_sca_event_name_from_type(sub.event), STR_FMT(&sub.subscriber));
				// fall through to unlock index
			}

			kz_sca_hash_table_unlock_index(sca->subscriptions, idx);
		}
	} while(db_fetch_next(scam->db_api, KZ_SCA_DB_DEFAULT_FETCH_ROW_COUNT, db_con, &result) == 1 && num_rows > 0);

	scam->db_api->free_result(db_con, result);

	// clear all records from table, let timer process repopulate it
	if(scam->db_api->delete(db_con, NULL, NULL, NULL, 0) < 0) {
		LM_ERR("kz_sca_subscriptions_restore_from_db: failed to delete records from table after restoring\n");
		goto done;
	}

	rc = 0;

done:
	scam->db_api->close(db_con);
	db_con = NULL;

	return (rc);
}

static int kz_sca_subscription_db_update_subscriber(db1_con_t *db_con, kz_sca_subscription_t *sub)
{
	db_key_t query_columns[1];
	db_val_t query_values[1];
	int query_column_idx = 0;
	db_key_t update_columns[6];
	db_val_t update_values[6];
	int update_column_idx = 0;

	if (!sca->db_persistance) return 0;

	assert(db_con != NULL);
	assert(sub != NULL);

	// bind the lookup key and value
	KZ_SCA_DB_BIND_STR_VALUE(sub->subscriber, &KZ_SCA_DB_SUBSCRIBER_COL_NAME, query_columns, query_values, query_column_idx);

	// bind updated keys and values
	KZ_SCA_DB_BIND_INT_VALUE(sub->expires, &KZ_SCA_DB_EXPIRES_COL_NAME, update_columns, update_values, update_column_idx);
	KZ_SCA_DB_BIND_STR_VALUE(sub->dialog.call_id, &KZ_SCA_DB_CALL_ID_COL_NAME, update_columns, update_values, update_column_idx);
	KZ_SCA_DB_BIND_STR_VALUE(sub->dialog.from_tag, &KZ_SCA_DB_FROM_TAG_COL_NAME, update_columns, update_values, update_column_idx);
	KZ_SCA_DB_BIND_STR_VALUE(sub->dialog.to_tag, &KZ_SCA_DB_TO_TAG_COL_NAME, update_columns, update_values, update_column_idx);
	KZ_SCA_DB_BIND_INT_VALUE(sub->dialog.notify_cseq + 1, &KZ_SCA_DB_NOTIFY_CSEQ_COL_NAME, update_columns, update_values, update_column_idx);
	KZ_SCA_DB_BIND_INT_VALUE(sub->dialog.subscribe_cseq + 1, &KZ_SCA_DB_SUBSCRIBE_CSEQ_COL_NAME, update_columns, update_values, update_column_idx);

	if(sca->db_api->update(db_con, query_columns, NULL, query_values, update_columns, update_values, query_column_idx, update_column_idx) < 0) {
		LM_ERR("kz_sca_subscription_db_update_subscriber: failed to update %s subscriber %.*s in DB\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
		return (-1);
	}

	// no further DB updates until subscription is changed
	sub->db_cmd_flag = KZ_SCA_DB_FLAG_NONE;

	return (0);
}

static int kz_sca_subscription_db_insert_subscriber(db1_con_t *db_con, kz_sca_subscription_t *sub)
{
	db_key_t insert_columns[KZ_SCA_DB_SUBSCRIPTIONS_NUM_COLUMNS];
	db_val_t insert_values[KZ_SCA_DB_SUBSCRIPTIONS_NUM_COLUMNS];
	str **column_names;
	int i;

	if (!sca->db_persistance) return 0;

	assert(db_con != NULL);
	assert(sub != NULL);

	column_names = kz_sca_db_subscriptions_columns();
	if(column_names == NULL) {
		LM_ERR("kz_sca_subscriptions_restore_from_db: failed to get column names for KZ_SCA subscriptions table\n");
		return (-1);
	}

	for(i = 0; i < KZ_SCA_DB_SUBSCRIPTIONS_NUM_COLUMNS; i++) {
		insert_columns[i] = column_names[i];
	}

	// XXX array boundary checking
	if(kz_sca_subscription_to_db_row_values(sub, insert_values) != 0) {
		LM_ERR("kz_sca_subscription_db_insert_subscriber: failed to set DB row values for INSERT of %s subscriber %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
		return (-1);
	}

	if(sca->db_api->insert(db_con, insert_columns, insert_values, KZ_SCA_DB_SUBSCRIPTIONS_NUM_COLUMNS) < 0) {
		LM_ERR("kz_sca_subscription_db_insert_subscriber: failed to insert %s subscriber %.*s in DB subscription table\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
		return (-1);
	}

	// no further DB actions needed until subscription is changed
	sub->db_cmd_flag = KZ_SCA_DB_FLAG_NONE;

	return (0);
}

int kz_sca_subscription_db_delete_expired(db1_con_t *db_con)
{
	db_key_t delete_columns[2];
	db_val_t delete_values[2];
	db_op_t delete_ops[2];
	time_t now = time(NULL);
	int kv_count = 0;

	if (!sca->db_persistance) return 0;

	delete_columns[kv_count] = (str *)&KZ_SCA_DB_SERVER_ID_COL_NAME;
	delete_ops[kv_count] = OP_EQ;

	KZ_SCA_DB_BIND_INT_VALUE(server_id, &KZ_SCA_DB_SERVER_ID_COL_NAME, delete_columns, delete_values, kv_count);

	delete_columns[kv_count] = (str *)&KZ_SCA_DB_EXPIRES_COL_NAME;
	delete_ops[kv_count] = OP_LT;

	KZ_SCA_DB_BIND_INT_VALUE(now, &KZ_SCA_DB_EXPIRES_COL_NAME, delete_columns, delete_values, kv_count);

	if(sca->db_api->delete(db_con, delete_columns, delete_ops, delete_values, kv_count) < 0) {
		LM_ERR("kz_sca_subscription_db_delete_expired: failed to delete subscriptions expired before %ld\n", (long int)now);
		return (-1);
	}

	return (0);
}

int kz_sca_subscription_db_update(void)
{
	db1_con_t *db_con = NULL;
	kz_sca_hash_table *ht;
	kz_sca_hash_entry *entry;
	kz_sca_subscription_t *sub;
	int i;
	int rc = -1;
	time_t now = time(NULL);

	if (!sca->db_persistance) return 0;

	db_con = kz_sca_db_get_connection();
	if(db_con == NULL) {
		LM_ERR("kz_sca_subscription_db_update: failed to connect to DB %.*s\n", STR_FMT(sca->cfg->db_url));
		goto done;
	}
	if(sca->db_api->use_table(db_con, sca->cfg->subs_table) < 0) {
		LM_ERR("kz_sca_subscription_db_update: failed to in-use table for DB %.*s\n", STR_FMT(sca->cfg->db_url));
		goto done;
	}

	ht = sca->subscriptions;
	for(i = 0; i < ht->size; i++) {
		kz_sca_hash_table_lock_index(ht, i);

		for(entry = ht->slots[i].entries; entry != NULL; entry = entry->next) {
			sub = (kz_sca_subscription_t *)entry->value;

			if(sub == NULL || sub->expires < now) {
				continue;
			}

			// we only do call-info subscriptions for now
			if(sub->event != KZ_SCA_EVENT_TYPE_CALL_INFO) {
				continue;
			}

			if(KZ_SCA_SUBSCRIPTION_IS_TERMINATED(sub)) {
				continue;
			}

			if(sub->db_cmd_flag == KZ_SCA_DB_FLAG_INSERT) {
				if(kz_sca_subscription_db_insert_subscriber(db_con, sub) < 0) {
					LM_ERR("kz_sca_subscription_db_update: failed to insert %s subscriber %.*s into subscription DB\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
				}
			} else if(sub->db_cmd_flag == KZ_SCA_DB_FLAG_UPDATE) {
				if(kz_sca_subscription_db_update_subscriber(db_con, sub) < 0) {
					LM_ERR("kz_sca_subscription_db_update: failed to insert %s subscriber %.*s into subscription DB\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
				}
			}
		}

		kz_sca_hash_table_unlock_index(ht, i);
	}

	rc = kz_sca_subscription_db_delete_expired(db_con);

done:
	return (rc);
}

void kz_sca_subscription_db_update_timer(unsigned int ticks, void *param)
{
	if (!sca->db_persistance) return;

	if(kz_sca_subscription_db_update() != 0) {
		LM_ERR("kz_sca_subscription_db_update_timer: failed to update subscriptions in DB %.*s\n", STR_FMT(sca->cfg->db_url));
	}
}

int kz_sca_subscription_aor_has_subscribers(int event, str *aor)
{
	kz_sca_hash_slot *slot;
	kz_sca_hash_entry *e;
	kz_sca_subscription_t *sub;
	str sub_key = STR_NULL;
	char *event_name;
	int len;
	int subscribers = 0;
	int slot_idx = -1;

	event_name = kz_sca_event_name_from_type(event);
	len = aor->len + strlen(event_name);
	sub_key.s = (char *)pkg_malloc(len);
	if(sub_key.s == NULL) {
		LM_ERR("Failed to pkg_malloc key to look up %s subscription for %.*s\n", event_name, STR_FMT(aor));
		return (-1);
	}
	KZ_SCA_STR_COPY(&sub_key, aor);
	KZ_SCA_STR_APPEND_CSTR(&sub_key, event_name);

	slot_idx = kz_sca_hash_table_index_for_key(sca->subscriptions, &sub_key);
	pkg_free(sub_key.s);
	sub_key.len = 0;

	slot = kz_sca_hash_table_slot_for_index(sca->subscriptions, slot_idx);
	kz_sca_hash_table_lock_index(sca->subscriptions, slot_idx);

	for(e = slot->entries; e != NULL; e = e->next) {
		sub = (kz_sca_subscription_t *)e->value;

		if(KZ_SCA_STR_EQ(&sub->target_aor, aor)) {
			subscribers = 1;
			break;
		}
	}

	kz_sca_hash_table_unlock_index(sca->subscriptions, slot_idx);

	return (subscribers);
}

kz_sca_subscription_t *kz_sca_subscription_create(kz_sca_subscription_t *req_sub, int opts)
{
	kz_sca_subscription_t *sub = NULL;
	int len = 0;

	len += sizeof(kz_sca_subscription_t);
	len += sizeof(char) * (req_sub->target_aor.len + req_sub->subscriber.len);

	if(!KZ_SCA_STR_EMPTY(&req_sub->rr)) {
		len += sizeof(char) * req_sub->rr.len;
	}

	if(!KZ_SCA_STR_EMPTY(&req_sub->endpoint_id)) {
		len += sizeof(char) * req_sub->endpoint_id.len;
	}

	if(!KZ_SCA_STR_EMPTY(&req_sub->sockinfo)) {
		len += sizeof(char) * req_sub->sockinfo.len;
	}

	if(!KZ_SCA_STR_EMPTY(&req_sub->user_agent)) {
		len += sizeof(char) * req_sub->user_agent.len;
	}

	sub = (kz_sca_subscription_t *)shm_malloc(len);
	if(sub == NULL) {
		LM_ERR("Failed to create %s subscription for %.*s: out of memory\n", kz_sca_event_name_from_type(req_sub->event), STR_FMT(&req_sub->subscriber));
		goto error;
	}
	memset(sub, 0, len);

	sub->event = req_sub->event;
	sub->state = KZ_SCA_SUBSCRIPTION_STATE_ACTIVE;
	sub->index = KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY;
	if(opts & KZ_SCA_SUBSCRIPTION_CREATE_OPT_RAW_EXPIRES) {
		sub->expires = req_sub->expires;
	} else {
		sub->expires = time(NULL) + req_sub->expires;
	}
	sub->dialog.notify_cseq = req_sub->dialog.notify_cseq;
	sub->dialog.subscribe_cseq = req_sub->dialog.subscribe_cseq;
	sub->db_cmd_flag = KZ_SCA_DB_FLAG_INSERT;

	len = sizeof(kz_sca_subscription_t);

	sub->subscriber.s = (char *)sub + len;
	KZ_SCA_STR_COPY(&sub->subscriber, &req_sub->subscriber);
	len += req_sub->subscriber.len;

	sub->target_aor.s = (char *)sub + len;
	KZ_SCA_STR_COPY(&sub->target_aor, &req_sub->target_aor);
	len += req_sub->target_aor.len;

	if(!KZ_SCA_STR_EMPTY(&req_sub->rr)) {
		sub->rr.s = (char *)sub + len;
		KZ_SCA_STR_COPY(&sub->rr, &req_sub->rr);
		len += req_sub->rr.len;
	}

	if(!KZ_SCA_STR_EMPTY(&req_sub->endpoint_id)) {
		sub->endpoint_id.s = (char *)sub + len;
		KZ_SCA_STR_COPY(&sub->endpoint_id, &req_sub->endpoint_id);
		len += req_sub->endpoint_id.len;
	}

	if(!KZ_SCA_STR_EMPTY(&req_sub->sockinfo)) {
		sub->sockinfo.s = (char *)sub + len;
		KZ_SCA_STR_COPY(&sub->sockinfo, &req_sub->sockinfo);
		len += req_sub->sockinfo.len;
	}

	if(!KZ_SCA_STR_EMPTY(&req_sub->user_agent)) {
		sub->user_agent.s = (char *)sub + len;
		KZ_SCA_STR_COPY(&sub->user_agent, &req_sub->user_agent);
		len += req_sub->user_agent.len;
	}


	// dialog.id holds call-id + from-tag + to-tag; dialog.call_id,
	// dialog.from_tag, and dialog.to_tag point to offsets within
	// dialog.id.
	//
	// we shm_malloc this separately in case we need to update in-memory
	// dialog saved for this subscriber. this is likely to happen if the
	// subscriber goes off-line for some reason.
	len = sizeof(char) * (req_sub->dialog.call_id.len + req_sub->dialog.from_tag.len + req_sub->dialog.to_tag.len);
	sub->dialog.id.s = (char *)shm_malloc(len);
	if(sub->dialog.id.s == NULL) {
		LM_ERR("Failed to shm_malloc space for %.*s %s subscription dialog: out of memory\n", STR_FMT(&sub->subscriber), kz_sca_event_name_from_type(sub->event));
		goto error;
	}
	sub->dialog.id.len = len;

	KZ_SCA_STR_COPY(&sub->dialog.id, &req_sub->dialog.call_id);
	KZ_SCA_STR_APPEND(&sub->dialog.id, &req_sub->dialog.from_tag);
	KZ_SCA_STR_APPEND(&sub->dialog.id, &req_sub->dialog.to_tag);

	sub->dialog.call_id.s = sub->dialog.id.s;
	sub->dialog.call_id.len = req_sub->dialog.call_id.len;

	sub->dialog.from_tag.s = sub->dialog.id.s + req_sub->dialog.call_id.len;
	sub->dialog.from_tag.len = req_sub->dialog.from_tag.len;

	sub->dialog.to_tag.s = sub->dialog.id.s + req_sub->dialog.call_id.len + req_sub->dialog.from_tag.len;
	sub->dialog.to_tag.len = req_sub->dialog.to_tag.len;

	sub->server_id = server_id;

	return (sub);

error:
	if(sub != NULL) {
		if(sub->dialog.id.s != NULL) {
			shm_free(sub->dialog.id.s);
		}
		shm_free(sub);
	}

	return (NULL);
}

int kz_sca_subscription_subscriber_cmp_endpoint_id(str *endpoint_id, void *cmp_value)
{
	kz_sca_subscription_t *sub = (kz_sca_subscription_t *)cmp_value;
	int cmp;

	if((cmp = endpoint_id->len - sub->endpoint_id.len) != 0) {
		return (cmp);
	}

	return (memcmp(endpoint_id->s, sub->endpoint_id.s, endpoint_id->len));
}

int kz_sca_subscription_subscriber_cmp_subscriber(str *subscriber, void *cmp_value)
{
	kz_sca_subscription_t *sub = (kz_sca_subscription_t *)cmp_value;
	int cmp;

	if((cmp = subscriber->len - sub->subscriber.len) != 0) {
		return (cmp);
	}

	return (memcmp(subscriber->s, sub->subscriber.s, subscriber->len));
}

int kz_sca_subscription_subscriber_cmp(str *subscriber, void *cmp_value)
{
	return kz_sca_subscription_subscriber_cmp_endpoint_id(subscriber, cmp_value);
}

void kz_sca_subscription_free(void *value)
{
	kz_sca_subscription_t *sub = (kz_sca_subscription_t *)value;

	if(sub == NULL) {
		return;
	}

	SCA_DBG("Freeing %s subscription from %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));

	if(!KZ_SCA_STR_EMPTY(&sub->dialog.id)) {
		shm_free(sub->dialog.id.s);
	}

	shm_free(sub);
}

void kz_sca_subscription_print(void *value)
{
	kz_sca_subscription_t *sub = (kz_sca_subscription_t *)value;

	SCA_DBG("%.*s %s (%d) %.*s, expires: %ld, index: %d, "
		   "dialog %.*s;%.*s;%.*s, record_route: %.*s, "
		   "notify_cseq: %d, subscribe_cseq: %d, server_id: %d, endpoint_id: %.*s\n",
			STR_FMT(&sub->target_aor), kz_sca_event_name_from_type(sub->event),
			sub->event, STR_FMT(&sub->subscriber), (long int)sub->expires,
			sub->index, STR_FMT(&sub->dialog.call_id),
			STR_FMT(&sub->dialog.from_tag), STR_FMT(&sub->dialog.to_tag),
			KZ_SCA_STR_EMPTY(&sub->rr) ? 4 : sub->rr.len,
			KZ_SCA_STR_EMPTY(&sub->rr) ? "null" : sub->rr.s,
			sub->dialog.notify_cseq, sub->dialog.subscribe_cseq,
			sub->server_id,
			KZ_SCA_STR_EMPTY(&sub->endpoint_id) ? 4 : sub->endpoint_id.len,
			KZ_SCA_STR_EMPTY(&sub->endpoint_id) ? "null" : sub->endpoint_id.s
			);
}

int kz_sca_subscription_save_unsafe(kz_sca_mod *scam, kz_sca_subscription_t *sub, int save_idx, int opts)
{
	kz_sca_subscription_t *new_sub = NULL;
	kz_sca_hash_slot *slot;
	int rc = -1;

	assert(save_idx >= 0);

	new_sub = kz_sca_subscription_create(sub, opts);
	if(new_sub == NULL) {
		return (-1);
	}
	if(sub->index != KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY) {
		new_sub->index = sub->index;
	}

	if(kz_sca_appearance_register(scam, &sub->target_aor) < 0) {
		LM_ERR("kz_sca_subscription_save: kz_sca_appearance_register failed, still saving subscription from %.*s\n", STR_FMT(&sub->endpoint_id));
	}

	slot = kz_sca_hash_table_slot_for_index(scam->subscriptions, save_idx);
	rc = kz_sca_hash_table_slot_kv_insert_unsafe(slot, new_sub, kz_sca_subscription_subscriber_cmp, kz_sca_subscription_print, kz_sca_subscription_free);
	if(rc < 0) {
		shm_free(new_sub);
		new_sub = NULL;
	}

	return (rc);
}

static int kz_sca_subscription_update_unsafe(kz_sca_mod *scam, kz_sca_subscription_t *saved_sub, kz_sca_subscription_t *update_sub, int sub_idx)
{
	int rc = -1;
	int len;
	char *dlg_id_tmp;

	if(sub_idx < 0 || sub_idx > scam->subscriptions->size) {
		LM_ERR("Invalid hash table index %d\n", sub_idx);
		goto done;
	}

	// sanity checks first
	if(saved_sub->event != update_sub->event) {
		LM_ERR("Event mismatch for in-dialog SUBSCRIBE from %.*s: %s != %s\n", STR_FMT(&update_sub->subscriber), kz_sca_event_name_from_type(saved_sub->event), kz_sca_event_name_from_type(update_sub->event));
		goto done;
	}
	if(!STR_EQ(saved_sub->subscriber, update_sub->subscriber)) {
		LM_ERR("Contact mismatch for in-dialog SUBSCRIBE from %.*s: %.*s != %.*s\n", STR_FMT(&update_sub->subscriber), STR_FMT(&update_sub->subscriber), STR_FMT(&saved_sub->subscriber));
		goto done;
	}
	if(!STR_EQ(saved_sub->target_aor, update_sub->target_aor)) {
		LM_ERR("AoR mismatch for in-dialog SUBSCRIBE from %.*s: %.*s != %.*s\n", STR_FMT(&update_sub->subscriber), STR_FMT(&update_sub->target_aor), STR_FMT(&saved_sub->target_aor));
		goto done;
	}

	if(!STR_EQ(saved_sub->dialog.call_id, update_sub->dialog.call_id)
			|| !STR_EQ(saved_sub->dialog.from_tag, update_sub->dialog.from_tag)
			|| !STR_EQ(saved_sub->dialog.to_tag, update_sub->dialog.to_tag)) {
		// mismatched dialog. we assume a subscriber can hold only one
		// subscription per event at any given time, so we replace the old
		// one with the new.
		assert(!KZ_SCA_STR_EMPTY(&saved_sub->dialog.id));

		// this is allocated separately from the rest of the subscription
		len = sizeof(char *)
			  * (update_sub->dialog.call_id.len
						+ update_sub->dialog.from_tag.len
						+ update_sub->dialog.to_tag.len);

		dlg_id_tmp = (char *)shm_malloc(len);
		if(dlg_id_tmp == NULL) {
			LM_ERR("Failed to replace %.*s %s subscription dialog: shm_malloc failed\n", STR_FMT(&update_sub->subscriber), kz_sca_event_name_from_type(update_sub->event));
			// XXX should remove subscription entirely here?
		} else {
			shm_free(saved_sub->dialog.id.s);
			saved_sub->dialog.id.s = dlg_id_tmp;
			saved_sub->dialog.id.len = len;

			KZ_SCA_STR_COPY(&saved_sub->dialog.id, &update_sub->dialog.call_id);
			KZ_SCA_STR_APPEND(&saved_sub->dialog.id, &update_sub->dialog.from_tag);
			KZ_SCA_STR_APPEND(&saved_sub->dialog.id, &update_sub->dialog.to_tag);

			saved_sub->dialog.call_id.s = saved_sub->dialog.id.s;
			saved_sub->dialog.call_id.len = update_sub->dialog.call_id.len;

			saved_sub->dialog.from_tag.s = saved_sub->dialog.id.s + update_sub->dialog.call_id.len;
			saved_sub->dialog.from_tag.len = update_sub->dialog.from_tag.len;

			saved_sub->dialog.to_tag.s = saved_sub->dialog.id.s + update_sub->dialog.call_id.len + update_sub->dialog.from_tag.len;
			saved_sub->dialog.to_tag.len = update_sub->dialog.to_tag.len;
		}
	}

	saved_sub->state = update_sub->state;
	saved_sub->dialog.subscribe_cseq = update_sub->dialog.subscribe_cseq;
	saved_sub->dialog.notify_cseq += 1;
	saved_sub->expires = time(NULL) + update_sub->expires;

	// flag subscription for DB update only if we've already inserted
	if(saved_sub->db_cmd_flag == KZ_SCA_DB_FLAG_NONE) {
		saved_sub->db_cmd_flag = KZ_SCA_DB_FLAG_UPDATE;
	}

	if(update_sub->index != KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY) {
		saved_sub->index = update_sub->index;
	}

	// set notify_cseq in update_sub, since we use it to send the NOTIFY
	update_sub->dialog.notify_cseq = saved_sub->dialog.notify_cseq;

	// ensure we send the NOTIFY back through the same path as the SUBSCRIBE
	if(KZ_SCA_STR_EMPTY(&update_sub->rr) && !KZ_SCA_STR_EMPTY(&saved_sub->rr)) {
		update_sub->rr.s = (char *)pkg_malloc(saved_sub->rr.len);
		if(update_sub->rr.s == NULL) {
			LM_ERR("kz_sca_subscription_update_unsafe: pkg_malloc record-route value %.*s failed\n", STR_FMT(&saved_sub->rr));
			goto done;
		}

		KZ_SCA_STR_COPY(&update_sub->rr, &saved_sub->rr);
	}

	rc = 1;

done:
	return (rc);
}

static int kz_sca_subscription_copy_subscription_key(kz_sca_subscription_t *sub, str *key_out)
{
	char *event_name;
	int len;

	assert(sub != NULL);
	assert(key_out != NULL);

	len = sub->target_aor.len;
	event_name = kz_sca_event_name_from_type(sub->event);
	len += strlen(event_name);

	key_out->s = (char *)pkg_malloc(len);
	if(key_out->s == NULL) {
		LM_ERR("Failed to pkg_malloc space for subscription key\n");
		return (-1);
	}

	KZ_SCA_STR_COPY(key_out, &sub->target_aor);
	KZ_SCA_STR_APPEND_CSTR(key_out, event_name);

	return (key_out->len);
}

int kz_sca_subscription_delete_subscriber_for_event_call_id(kz_sca_mod *scam, str *subscriber, str *event, str *aor, str *call_id)
{
	kz_sca_hash_slot *slot;
	kz_sca_hash_entry *ent;
	kz_sca_subscription_t *sub;
	str subkey = STR_NULL;
	char skbuf[1024];
	int slot_idx;
	int len;
	int remain = 0;

	len = aor->len;
	len += event->len;

	if(len >= sizeof(skbuf)) {
		LM_ERR("Subscription key %.*s%.*s: too long\n", STR_FMT(aor), STR_FMT(event));
		return (-1);
	}

	subkey.s = skbuf;
	KZ_SCA_STR_COPY(&subkey, aor);
	KZ_SCA_STR_APPEND(&subkey, event);

	slot_idx = kz_sca_hash_table_index_for_key(scam->subscriptions, &subkey);

	slot = kz_sca_hash_table_slot_for_index(sca->subscriptions, slot_idx);
	kz_sca_hash_table_lock_index(scam->subscriptions, slot_idx);

	ent = kz_sca_hash_table_slot_kv_find_entry_unsafe(slot, subscriber);
	if(ent != NULL) {
		kz_sca_hash_entry *iterator = ent;
		ent = NULL;
		SCA_DBG("STARTING DELETE ITERATOR\n");
		while (iterator) {
			sub = (kz_sca_subscription_t *) iterator->value;
			SCA_DBG("DELETE ITERATOR\n");
			if (KZ_SCA_STR_EQ(&sub->endpoint_id, subscriber) && KZ_SCA_STR_EQ(&sub->target_aor, aor)) {
				SCA_DBG("SAME SUBSCRIBER AND AOR %.*s , %.*s \n", STR_FMT(subscriber), STR_FMT(aor));
				if (KZ_SCA_STR_EQ(&sub->dialog.call_id, call_id)) {
					SCA_DBG("SAME CALL-ID %.*s\n", STR_FMT(call_id));
					ent = iterator;
					iterator = iterator->next;
					ent = kz_sca_hash_table_slot_unlink_entry_unsafe(slot, ent);
				} else {
					SCA_DBG("DIFFERENT CALL-ID '%.*s' => '%.*s'\n", STR_FMT(&sub->dialog.call_id), STR_FMT(call_id));
					remain++;
					iterator = iterator->next;
				}
			} else {
				SCA_DBG("SUBSCRIBER AND/OR AOR ARE DIFFERENT %.*s => %.*s , %.*s => %.*s\n", STR_FMT(&sub->endpoint_id), STR_FMT(subscriber), STR_FMT(&sub->target_aor), STR_FMT(aor));
				iterator = iterator->next;
			}
		}
	}

	kz_sca_hash_table_unlock_index(sca->subscriptions, slot_idx);

	if(ent != NULL) {
		kz_sca_hash_entry_free(ent);
	}

	return remain;
}

int kz_sca_subscription_delete_subscriber_for_event(kz_sca_mod *scam, str *subscriber, str *event, str *aor)
{
	kz_sca_hash_slot *slot;
	kz_sca_hash_entry *ent;
	str subkey = STR_NULL;
	char skbuf[1024];
	int slot_idx;
	int len;

	len = aor->len;
	len += event->len;

	if(len >= sizeof(skbuf)) {
		LM_ERR("Subscription key %.*s%.*s: too long\n", STR_FMT(aor), STR_FMT(event));
		return (-1);
	}

	subkey.s = skbuf;
	KZ_SCA_STR_COPY(&subkey, aor);
	KZ_SCA_STR_APPEND(&subkey, event);

	slot_idx = kz_sca_hash_table_index_for_key(scam->subscriptions, &subkey);

	slot = kz_sca_hash_table_slot_for_index(sca->subscriptions, slot_idx);
	kz_sca_hash_table_lock_index(scam->subscriptions, slot_idx);

	ent = kz_sca_hash_table_slot_kv_find_entry_unsafe(slot, subscriber);
	if(ent != NULL) {
		ent = kz_sca_hash_table_slot_unlink_entry_unsafe(slot, ent);
	}

	kz_sca_hash_table_unlock_index(sca->subscriptions, slot_idx);

	if(ent != NULL) {
		kz_sca_hash_entry_free(ent);
	}

	return (1);
}

void kz_sca_subscribe_endpoint_id(sip_msg_t *msg, str * endpoint_id)
{
	char buffer[1024];
	str endpoint_id_str;

	endpoint_id_str.s = buffer;
	endpoint_id_str.len = sprintf(buffer, "%.*s", STR_FMT(&cfg_get(kz_sca, kz_sca_cfg, endpoint_spec)));
	

	pv_spec_t endpoint_id_spec;
	if (pv_parse_spec(&endpoint_id_str, &endpoint_id_spec) != NULL) {
		pv_value_t pv_val;
		if (pv_get_spec_value(msg, &endpoint_id_spec, &pv_val) == 0) {
			if ((pv_val.flags & PV_VAL_STR)) {
				if (pv_val.rs.len > 0 && pv_val.rs.s) {
					endpoint_id->s = pkg_malloc(pv_val.rs.len + 1);
					KZ_SCA_STR_COPY(endpoint_id, &pv_val.rs);
				}
			}
		}
	}
}

/*
 * caller must pkg_free req_sub->rr.s and req_sub->dialog.to_tag.s
 */
int kz_sca_subscription_from_request(kz_sca_mod *scam, sip_msg_t *msg, int event_type, kz_sca_subscription_t *req_sub)
{
	kz_sca_to_body_t to, from;
	str contact_uri;
	str to_tag = STR_NULL;
	unsigned int expires = 0, max_expires;
	unsigned int cseq;
	str *ruri = NULL;
	int rc = 1;

	assert(req_sub != NULL);

	memset(req_sub, 0, sizeof(kz_sca_subscription_t));
	memset(&from, 0, sizeof(from));
	memset(&to, 0, sizeof(to));

	// parse required info first
	if(!KZ_SCA_HEADER_EMPTY(msg->expires)) {
		if(parse_expires(msg->expires) < 0) {
			LM_ERR("Failed to parse Expires header\n");
			goto error;
		}

		expires = ((exp_body_t *)msg->expires->parsed)->val;
	}

	switch(event_type) {
		case KZ_SCA_EVENT_TYPE_CALL_INFO:
		default:
			max_expires = scam->cfg->call_info_max_expires;
			break;

		case KZ_SCA_EVENT_TYPE_LINE_SEIZE:
			max_expires = scam->cfg->line_seize_max_expires;
			break;
	}

	if(expires && expires > max_expires) {
		expires = max_expires;
	}

	if(KZ_SCA_HEADER_EMPTY(msg->callid)) {
		LM_ERR("Empty Call-ID header\n");
		goto error;
	}

	// XXX move to static inline function
	if(KZ_SCA_HEADER_EMPTY(msg->cseq)) {
		LM_ERR("Empty CSeq header\n");
		goto error;
	}
	if(str2int(&(get_cseq(msg)->number), &cseq) != 0) {
		LM_ERR("Bad Cseq header: %.*s\n", msg->cseq->body.len, msg->cseq->body.s);
		goto error;
	}

	if(kz_sca_get_msg_contact_uri(msg, &contact_uri) < 0) {
		// above logs error
		goto error;
	}

	if(kz_sca_get_msg_from_header(msg, &from) < 0) {
		LM_ERR("Bad From header\n");
		goto error;
	}

	if(KZ_SCA_STR_EMPTY(&from.hdr->tag_value)) {
		LM_ERR("No from-tag in From header\n");
		goto error;
	}

	if(kz_sca_get_msg_to_header(msg, &to) < 0) {
		LM_ERR("Bad To header");
		goto error;
	}

	if(parse_sip_msg_uri(msg) < 0) {
		LM_ERR("Error while parsing the Request-URI\n");
		goto error;
	}
	ruri = GET_RURI(msg);

	to_tag = to.hdr->tag_value;
	if(to_tag.s == NULL) {
		// XXX need hook to detect when we have a subscription and the
		// subscriber sends an out-of-dialog SUBSCRIBE, which indicates the
		// old subscription should be dumped & appropriate NOTIFYs sent.
		if(scam->sl_api->get_reply_totag(msg, &to_tag) < 0) {
			LM_ERR("Failed to generate to-tag for reply to SUBSCRIBE %.*s\n", STR_FMT(ruri));
			goto error;
		}

		if(!KZ_SCA_HEADER_EMPTY(msg->record_route)) {
			if(print_rr_body(msg->record_route, &req_sub->rr, 0, NULL) < 0) {
				LM_ERR("Failed to parse Record-Route header %.*s in SUBSCRIBE %.*s from %.*s\n", STR_FMT(&msg->record_route->body), STR_FMT(ruri), STR_FMT(&contact_uri));
				goto error;
			}
		}
		req_sub->target_aor = to.hdr->uri;
	} else {
		/* we are in-dialog */
		req_sub->target_aor = to.hdr->uri;
	}

	req_sub->subscriber = contact_uri;
	req_sub->event = event_type;
	req_sub->index = KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY;
	req_sub->expires = expires;
	if(req_sub->expires > 0) {
		req_sub->state = KZ_SCA_SUBSCRIPTION_STATE_ACTIVE;
		expires += time(NULL);
	} else {
		// subscriber requested subscription termination, see rfc3265 3.2.4
		req_sub->state = KZ_SCA_SUBSCRIPTION_STATE_TERMINATED;
	}

	req_sub->dialog.id.s = NULL;
	req_sub->dialog.id.len = 0;
	req_sub->dialog.call_id = msg->callid->body;
	req_sub->dialog.from_tag = from.hdr->tag_value;

	req_sub->dialog.to_tag.s = pkg_malloc(to_tag.len);
	if(req_sub->dialog.to_tag.s == NULL) {
		LM_ERR("Failed to pkg_malloc space for to-tag %.*s\n", STR_FMT(&to_tag));
		goto error;
	}
	KZ_SCA_STR_COPY(&req_sub->dialog.to_tag, &to_tag);

	req_sub->dialog.subscribe_cseq = 0;
	req_sub->dialog.notify_cseq = 0;
	req_sub->server_id = server_id;

	kz_sca_subscribe_endpoint_id(msg, &req_sub->endpoint_id);

	if(parse_headers(msg, HDR_USERAGENT_F, 0) != -1 && msg->user_agent && msg->user_agent->body.len > 0 && msg->user_agent->body.len < KZ_SCA_MAX_UA_SIZE) {
		req_sub->user_agent = msg->user_agent->body;
	}

	req_sub->sockinfo = msg->rcv.bind_address->sock_str;

	goto done;

error:
	rc = -1;
	if(!KZ_SCA_STR_EMPTY(&req_sub->rr)) {
		pkg_free(req_sub->rr.s);
		req_sub->rr.s = NULL;
	}
done:
	if(from.flags & KZ_SCA_UTIL_FLAG_TO_BODY_ALLOC) {
		free_to(from.hdr);
	}
	if(to.flags & KZ_SCA_UTIL_FLAG_TO_BODY_ALLOC) {
		free_to(to.hdr);
	}

	return rc;
}

int kz_sca_handle_subscribe(sip_msg_t *msg)
{
	kz_sca_subscription_t req_sub;
	kz_sca_subscription_t *sub = NULL;
	kz_sca_call_info call_info;
	hdr_field_t *call_info_hdr = NULL;
	str sub_key = STR_NULL;
	str *to_tag = NULL;
	char *status_text;
	int event_type;
	int status;
	int app_idx = KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY;
	int idx = -1;
	int rc = -1;
	int released = 0;
	kz_sca_to_body_t tmp_to;
	kz_sca_hash_slot *slot = NULL;
	kz_sca_hash_entry *ent = NULL;
	int skip_notify = 0;

	memset(&tmp_to, 0, sizeof(tmp_to));
	if(parse_headers(msg, HDR_EOH_F, 0) < 0) {
		LM_ERR("header parsing failed: bad request\n");
		KZ_SCA_SUB_REPLY_ERROR(sca, 400, "Bad Request", msg);
		return (-1);
	}

	if(!STR_EQ(REQ_LINE(msg).method, KZ_SCA_METHOD_SUBSCRIBE)) {
		LM_ERR("bad request method %.*s\n", STR_FMT(&REQ_LINE(msg).method));
		KZ_SCA_SUB_REPLY_ERROR(sca, 500, "Internal server error - config", msg);
		return (-1);
	}

	if(KZ_SCA_HEADER_EMPTY(msg->event)) {
		KZ_SCA_SUB_REPLY_ERROR(sca, 400, "Missing Event", msg);
		return (-1);
	}

	event_type = kz_sca_event_from_str(&msg->event->body);
	if(event_type == KZ_SCA_EVENT_TYPE_UNKNOWN) {
		KZ_SCA_SUB_REPLY_ERROR(sca, 400, "Bad Event", msg);
		return (-1);
	}

	if(kz_sca_subscription_from_request(sca, msg, event_type, &req_sub) < 0) {
		KZ_SCA_SUB_REPLY_ERROR(sca, 400, "Bad Shared Call Appearance Request", msg);
		return (-1);
	}

	if(kz_sca_subscription_copy_subscription_key(&req_sub, &sub_key) < 0) {
		KZ_SCA_SUB_REPLY_ERROR(sca, 500, "Internal Server Error - copy dialog id", msg);
		goto done;
	}

	kz_sca_subscription_print(&req_sub);

	// check to see if the message has a to-tag
	to_tag = &(get_to(msg)->tag_value);

	// XXX should lock starting here and use unsafe methods below?

	// ensure we only calculate the hash table index once
	idx = kz_sca_hash_table_index_for_key(sca->subscriptions, &sub_key);
	// pkg_malloc'd in kz_sca_subscription_copy_subscription_key above
	pkg_free(sub_key.s);

	if(req_sub.event == KZ_SCA_EVENT_TYPE_LINE_SEIZE) {
		call_info_hdr = get_hdr(msg, HDR_CALLINFO_T);
		if(call_info_hdr) {
			if(kz_sca_call_info_body_parse(&call_info_hdr->body, &call_info) < 0) {
				KZ_SCA_SUB_REPLY_ERROR(sca, 400, "Bad Request - Invalid Call-Info header", msg);
				goto done;
			}
			req_sub.index = app_idx = call_info.index;
		} else {
			KZ_SCA_SUB_REPLY_ERROR(sca, 400, "Bad Request - missing Call-Info header", msg);
			goto done;
		}
	}

	slot = kz_sca_hash_table_slot_for_index(sca->subscriptions, idx);
	kz_sca_hash_table_lock_index(sca->subscriptions, idx);

	// require authorized endpoint
	if (!req_sub.endpoint_id.len || !req_sub.endpoint_id.s) {
		LM_WARN("endpoint_id is empty for aor %.*s to_tag: %.*s\n", STR_FMT(&req_sub.target_aor), STR_FMT(to_tag));
		KZ_SCA_SUB_REPLY_ERROR(sca, 403, "Forbidden", msg);
		goto done;
	}

	ent = kz_sca_hash_table_slot_kv_find_entry_unsafe(slot, &req_sub.endpoint_id);
	if(ent != NULL) {
		kz_sca_hash_entry *iterator = ent;
		while (iterator) {
			kz_sca_subscription_t *s = (kz_sca_subscription_t *)iterator->value;
			if (KZ_SCA_STR_EQ(&s->endpoint_id, &req_sub.endpoint_id) && KZ_SCA_STR_EQ(&s->target_aor, &req_sub.target_aor)) {
				SCA_DBG("SAME SUBSCRIBER AND AOR %.*s , %.*s \n", STR_FMT(&s->endpoint_id), STR_FMT(&s->target_aor));
				if (KZ_SCA_STR_EQ(&s->dialog.call_id, &req_sub.dialog.call_id)) {
					SCA_DBG("SAME SUBSCRIBER AND AOR AND CALL-ID %.*s , %.*s , %.*s \n", STR_FMT(&s->endpoint_id), STR_FMT(&s->target_aor), STR_FMT(&s->dialog.call_id));
					sub = s;
					break;
				} else {
					SCA_DBG("SAME SUBSCRIBER AND AOR DIFFERENT CALL-ID %.*s , %.*s , %.*s => %.*s \n", STR_FMT(&s->endpoint_id), STR_FMT(&s->target_aor), STR_FMT(&s->dialog.call_id), STR_FMT(&req_sub.dialog.call_id));
				}
			} else {
				SCA_DBG("DIFFERENT SUBSCRIBER AND AOR %.*s => %.*s , %.*s => %.*s\n", STR_FMT(&s->endpoint_id), STR_FMT(&req_sub.endpoint_id), STR_FMT(&s->target_aor), STR_FMT(&req_sub.target_aor));
			}
			iterator = iterator->next;
		}
	}

	if(sub != NULL) {
		SCA_DBG("kz_sca_handle_subscribe: subscription[%.*s] %.*s found\n", STR_FMT(&req_sub.subscriber), STR_FMT(&req_sub.endpoint_id));
		// this will remove the subscription if expires == 0
		if(kz_sca_subscription_update_unsafe(sca, sub, &req_sub, idx) < 0) {
			KZ_SCA_SUB_REPLY_ERROR(sca, 500, "Internal Server Error - update subscription", msg);
			SCA_DBG("kz_sca_handle_subscribe: error updating subscription[%.*s] %.*s found\n", STR_FMT(&req_sub.subscriber), STR_FMT(&req_sub.endpoint_id));
			goto done;
		}

		if(req_sub.event == KZ_SCA_EVENT_TYPE_LINE_SEIZE) {
			if(req_sub.expires == 0) {
				// release the seized appearance
				if(call_info_hdr == NULL) {
					SCA_DBG("kz_sca_handle_subscribe: missing Call-Info when releasing seized appearance index [%.*s]\n", STR_FMT(&req_sub.target_aor));
					KZ_SCA_SUB_REPLY_ERROR(sca, 400, "Bad Request - missing Call-Info header", msg);
					goto done;
				}

				SCA_DBG("kz_sca_handle_subscribe: releasing seized appearance index %d [%.*s]\n", call_info.index, STR_FMT(&req_sub.target_aor));

				if(kz_sca_appearance_release_index(sca, &req_sub.target_aor, call_info.index) != KZ_SCA_APPEARANCE_OK) {
					KZ_SCA_SUB_REPLY_ERROR(sca, 500, "Internal Server Error - release seized line", msg);
					SCA_DBG("kz_sca_handle_subscribe: error releasing seized appearance index %d [%.*s]\n", call_info.index, STR_FMT(&req_sub.target_aor));
					goto done;
				}

			} else if(KZ_SCA_STR_EMPTY(to_tag)) {
				// don't seize new index if this is a line-seize reSUBSCRIBE
				//app_idx = kz_sca_appearance_seize_index(sca, &req_sub.target_aor, app_idx, &req_sub.subscriber);
				app_idx = kz_sca_appearance_seize_index(sca, &req_sub.target_aor, app_idx, &req_sub.endpoint_id);
				if(app_idx == KZ_SCA_APPEARANCE_INDEX_UNAVAILABLE) {
					KZ_SCA_SUB_REPLY_ERROR(sca, 480, "Temporarily Unavailable", msg);
					goto done;
				} else if(app_idx < 0) {
					KZ_SCA_SUB_REPLY_ERROR(sca, 500, "Internal Server Error - seize appearance index", msg);
					goto done;
				}
				req_sub.index = app_idx;
			}
		} else {
			if(KZ_SCA_STR_EMPTY(to_tag)) {
				// if the subscriber owns any active appearances, clear them.
				// we assume that an out-of-dialog SUBSCRIBE for a subscriber
				// with active appearances is indicative of a reboot.
				//released = kz_sca_appearance_owner_release_all(&req_sub.target_aor, &req_sub.subscriber);
				released = kz_sca_appearance_owner_release_all(&req_sub.target_aor, &req_sub.endpoint_id);
				if(released) {
					LM_INFO("kz_sca_handle_subscribe: released %d appearances for subscriber %.*s\n", released, STR_FMT(&req_sub.endpoint_id));
					//LM_INFO("kz_sca_handle_subscribe: released %d appearances for subscriber %.*s\n", released, STR_FMT(&req_sub.subscriber));
				} else {
					// SCA_DBG("kz_sca_handle_subscribe: subscriber[%.*s] doesn't own any active appearances using target[%.*s]\n", STR_FMT(&req_sub.subscriber), STR_FMT(&req_sub.target_aor));
					SCA_DBG("kz_sca_handle_subscribe: subscriber[%.*s] doesn't own any active appearances using target[%.*s]\n", STR_FMT(&req_sub.endpoint_id), STR_FMT(&req_sub.target_aor));
				}
			}
		}

		if(req_sub.expires == 0) {
			ent = kz_sca_hash_table_slot_unlink_entry_unsafe(slot, ent);
			sub->expires = 0;
			sub->dialog.notify_cseq += 1;
			sub->state = KZ_SCA_SUBSCRIPTION_STATE_TERMINATED;
			if(ent) {
				kz_sca_hash_entry_free(ent);
			}
		}

	} else {
		// in-dialog request, but we didn't find it.
		if(!KZ_SCA_STR_EMPTY(to_tag)) {
			SCA_DBG("kz_sca_handle_subscribe: subscription[%.*s] %.*s not found\n", STR_FMT(&req_sub.subscriber), STR_FMT(&req_sub.endpoint_id));
			KZ_SCA_SUB_REPLY_ERROR(sca, 481, "Call Leg/Transaction Does Not Exist", msg);
			goto done;
		}

		if(req_sub.expires > 0) {
			if(req_sub.event == KZ_SCA_EVENT_TYPE_LINE_SEIZE) {
				//app_idx = kz_sca_appearance_seize_index(sca, &req_sub.target_aor, app_idx, &req_sub.subscriber);
				app_idx = kz_sca_appearance_seize_index(sca, &req_sub.target_aor, app_idx, &req_sub.endpoint_id);
				if(app_idx == KZ_SCA_APPEARANCE_INDEX_UNAVAILABLE) {
					KZ_SCA_SUB_REPLY_ERROR(sca, 480, "Temporarily Unavailable", msg);
					goto done;
				} else if(app_idx < 0) {
					KZ_SCA_SUB_REPLY_ERROR(sca, 500, "Internal Server Error - seize appearance index", msg);
					goto done;
				}
				req_sub.index = app_idx;
			}

			if(kz_sca_subscription_save_unsafe(sca, &req_sub, idx, KZ_SCA_SUBSCRIPTION_CREATE_OPT_DEFAULT) < 0) {
				KZ_SCA_SUB_REPLY_ERROR(sca, 500, "Internal Server Error - save subscription", msg);
				goto done;
			}
		} else {
			// we got an in-dialog SUBSCRIBE with an "Expires: 0" header,
			// but the dialog wasn't in our table. just reply with the
			// subscription info we got, without saving or creating anything.
			SCA_DBG("kz_sca_handle_subscribe: expires=0 in-dialog but dialog not found\n");
			sub = &req_sub;
		}
	}

	kz_sca_hash_table_unlock_index(sca->subscriptions, idx);
	idx = -1;

	status = kz_sca_ok_status_for_event(event_type);
	status_text = kz_sca_ok_text_for_event(event_type);
	if(kz_sca_subscription_reply(sca, status, status_text, event_type, req_sub.expires, msg) < 0) {
		KZ_SCA_SUB_REPLY_ERROR(sca, 500, "Internal server error", msg);
		goto done;
	}

	if (!skip_notify) {
		if(kz_sca_notify_subscriber(sca, &req_sub, app_idx) < 0) {
			//LM_ERR("KZ_SCA %s SUBSCRIBE+NOTIFY for %.*s failed\n", kz_sca_event_name_from_type(req_sub.event), STR_FMT(&req_sub.subscriber));
			LM_ERR("KZ_SCA %s SUBSCRIBE+NOTIFY for %.*s failed\n", kz_sca_event_name_from_type(req_sub.event), STR_FMT(&req_sub.endpoint_id));
			// XXX - what does subscriber do in this case? drop subscription?
			// sub is already saved/updated in hash table. let it rot?
			goto done;
		}
	}

//	if(req_sub.event == KZ_SCA_EVENT_TYPE_LINE_SEIZE || released) {
	/*
	if(req_sub.event == KZ_SCA_EVENT_TYPE_LINE_SEIZE) {
		if(kz_sca_notify_call_info_subscribers(sca, &req_sub.target_aor) < 0) {
			LM_ERR("KZ_SCA %s NOTIFY to all %.*s subscribers failed\n", kz_sca_event_name_from_type(req_sub.event), STR_FMT(&req_sub.target_aor));
			goto done;
		}
	}
	*/

	rc = 1;

done:
	if(idx >= 0) {
		kz_sca_hash_table_unlock_index(sca->subscriptions, idx);
	}

	if(req_sub.dialog.to_tag.s != NULL) {
		pkg_free(req_sub.dialog.to_tag.s);
	}
	if(req_sub.rr.s != NULL) {
		pkg_free(req_sub.rr.s);
	}
	if(req_sub.endpoint_id.s != NULL) {
		pkg_free(req_sub.endpoint_id.s);
	}
	if(tmp_to.flags & KZ_SCA_UTIL_FLAG_TO_BODY_ALLOC) {
		free_to(tmp_to.hdr);
	}
	return (rc);
}

int kz_sca_subscription_reply(kz_sca_mod *scam, int status_code, char *status_msg, int event_type, int expires, sip_msg_t *msg)
{
	str extra_headers = STR_NULL;
	char hdr_buf[1024];
	int len;

	if(event_type != KZ_SCA_EVENT_TYPE_CALL_INFO && event_type != KZ_SCA_EVENT_TYPE_LINE_SEIZE) {
		LM_ERR("kz_sca_subscription_reply: unrecognized event type %d\n", event_type);
		return (-1);
	}

	if(status_code < 300) {
		// Add Event, Contact, Allow-Events and Expires headers
		extra_headers.s = hdr_buf;
		len = snprintf(extra_headers.s, sizeof(hdr_buf), "Event: %s%s", kz_sca_event_name_from_type(event_type), CRLF);
		if(len >= sizeof(hdr_buf) || len < 0) {
			LM_ERR("kz_sca_subscription_reply: extra headers too long\n");
			return (-1);
		}
		extra_headers.len = len;

		KZ_SCA_STR_APPEND_CSTR(&extra_headers, "Contact: ");
		if(sca->cfg->server_address != NULL) {
			KZ_SCA_STR_APPEND(&extra_headers, sca->cfg->server_address);
		} else {
			KZ_SCA_STR_APPEND(&extra_headers, &REQ_LINE(msg).uri);
		}
		KZ_SCA_STR_APPEND_CSTR(&extra_headers, CRLF);

		KZ_SCA_STR_COPY_CSTR(&extra_headers, "Allow-Events: call-info, line-seize" CRLF);

		len = snprintf(extra_headers.s + extra_headers.len, sizeof(hdr_buf) - extra_headers.len, "Expires: %d%s", expires, CRLF);
		if(len >= (sizeof(hdr_buf) - extra_headers.len) || len < 0) {
			LM_ERR("kz_sca_subscription_reply: extra headers too long\n");
			return (-1);
		}
		extra_headers.len += len;
	} else if(status_code == 480) {
		// tell loser of line-seize SUBSCRIBE race to try again shortly
		extra_headers.s = hdr_buf;
		len = snprintf(extra_headers.s, sizeof(hdr_buf), "Retry-After: %d%s", 1, CRLF);
		extra_headers.len = len;
	}

	return (kz_sca_reply(scam, status_code, status_msg, &extra_headers, msg));
}

/*
 * return values:
 *	-1: error
 *	 0: no subscription found to terminate
 *	 1: subscription terminated
 */
int kz_sca_subscription_terminate(kz_sca_mod *scam, str *aor, int event, str *subscriber, int termination_state, int opts)
{
	kz_sca_hash_slot *slot;
	kz_sca_hash_entry *ent;
	kz_sca_subscription_t *sub;
	str sub_key = STR_NULL;
	char *event_name;
	int slot_idx;
	int len;

	if(!(opts & KZ_SCA_SUBSCRIPTION_TERMINATE_OPT_UNSUBSCRIBE)) {
		LM_ERR("kz_sca_subscription_terminate: invalid opts 0x%x\n", opts);
		return (-1);
	}

	event_name = kz_sca_event_name_from_type(event);
	len = aor->len + strlen(event_name);
	sub_key.s = (char *)pkg_malloc(len);
	if(sub_key.s == NULL) {
		LM_ERR("Failed to pkg_malloc key to look up %s subscription for %.*s\n", event_name, STR_FMT(aor));
		return (-1);
	}
	KZ_SCA_STR_COPY(&sub_key, aor);
	KZ_SCA_STR_APPEND_CSTR(&sub_key, event_name);

	slot_idx = kz_sca_hash_table_index_for_key(scam->subscriptions, &sub_key);
	pkg_free(sub_key.s);
	sub_key.len = 0;

	slot = kz_sca_hash_table_slot_for_index(sca->subscriptions, slot_idx);
	kz_sca_hash_table_lock_index(scam->subscriptions, slot_idx);

	ent = kz_sca_hash_table_slot_kv_find_entry_unsafe(slot, subscriber);
	if(ent != NULL) {
		ent = kz_sca_hash_table_slot_unlink_entry_unsafe(slot, ent);
	}

	kz_sca_hash_table_unlock_index(sca->subscriptions, slot_idx);

	if(ent == NULL) {
		SCA_DBG("No %s subscription for %.*s\n", event_name, STR_FMT(subscriber));
		return (0);
	}

	SCA_DBG("%s subscription found for %.*s\n", event_name, STR_FMT(subscriber));

	sub = (kz_sca_subscription_t *)ent->value;
	sub->expires = 0;
	sub->dialog.notify_cseq += 1;
	sub->state = termination_state;

	kz_sca_subscription_print(sub);

	if(kz_sca_notify_subscriber(sca, sub, sub->index) < 0) {
		LM_ERR("KZ_SCA %s NOTIFY to %.*s failed\n", event_name, STR_FMT(&sub->subscriber));

		// fall through, we might be able to notify the others
	}

	if((opts & KZ_SCA_SUBSCRIPTION_TERMINATE_OPT_RELEASE_APPEARANCE) && sub->index != KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY) {
		SCA_DBG("releasing index %i from %s subscription found for %.*s\n", sub->index, event_name, STR_FMT(subscriber));
		if(kz_sca_appearance_release_index(sca, &sub->target_aor, sub->index) == KZ_SCA_APPEARANCE_OK) {
			SCA_DBG("index %i released from %s subscription found for %.*s\n", sub->index, event_name, STR_FMT(subscriber));
			if(kz_sca_notify_call_info_subscribers(sca, &sub->target_aor) < 0) {
				LM_ERR("KZ_SCA %s NOTIFY to all %.*s subscribers failed\n", event_name, STR_FMT(&sub->target_aor));
				// fall through, not much we can do about it
			}
		}
	}

	if(ent) {
		kz_sca_hash_entry_free(ent);
	}

	return (1);
}
