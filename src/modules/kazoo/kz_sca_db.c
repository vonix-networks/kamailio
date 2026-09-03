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

#include "kz_sca_db.h"
#include "kz_sca_subscribe.h"

db1_con_t *kz_sca_db_con = NULL;

const str KZ_SCA_DB_SUBSCRIBER_COL_NAME = STR_STATIC_INIT("subscriber");
const str KZ_SCA_DB_AOR_COL_NAME = STR_STATIC_INIT("aor");
const str KZ_SCA_DB_EVENT_COL_NAME = STR_STATIC_INIT("event");
const str KZ_SCA_DB_EXPIRES_COL_NAME = STR_STATIC_INIT("expires");
const str KZ_SCA_DB_STATE_COL_NAME = STR_STATIC_INIT("state");
const str KZ_SCA_DB_APP_IDX_COL_NAME = STR_STATIC_INIT("app_idx");
const str KZ_SCA_DB_CALL_ID_COL_NAME = STR_STATIC_INIT("call_id");
const str KZ_SCA_DB_FROM_TAG_COL_NAME = STR_STATIC_INIT("from_tag");
const str KZ_SCA_DB_TO_TAG_COL_NAME = STR_STATIC_INIT("to_tag");
const str KZ_SCA_DB_RECORD_ROUTE_COL_NAME = STR_STATIC_INIT("record_route");
const str KZ_SCA_DB_NOTIFY_CSEQ_COL_NAME = STR_STATIC_INIT("notify_cseq");
const str KZ_SCA_DB_SUBSCRIBE_CSEQ_COL_NAME = STR_STATIC_INIT("subscribe_cseq");
const str KZ_SCA_DB_SERVER_ID_COL_NAME = STR_STATIC_INIT("server_id");
const str KZ_SCA_DB_ENDPOINT_ID_COL_NAME = STR_STATIC_INIT("endpoint_id");
const str KZ_SCA_DB_SOCKET_INFO_COL_NAME = STR_STATIC_INIT("sockinfo");
const str KZ_SCA_DB_USER_AGENT_COL_NAME = STR_STATIC_INIT("ua");

void kz_sca_db_subscriptions_get_value_for_column(int column, db_val_t *row_values, void *column_value)
{
	assert(column_value != NULL);
	assert(row_values != NULL);
	assert(column >= 0 && column < KZ_SCA_DB_SUBS_BOUNDARY);

	switch(column) {
		case KZ_SCA_DB_SUBS_SUBSCRIBER_COL:
		case KZ_SCA_DB_SUBS_AOR_COL:
		case KZ_SCA_DB_SUBS_CALL_ID_COL:
		case KZ_SCA_DB_SUBS_FROM_TAG_COL:
		case KZ_SCA_DB_SUBS_TO_TAG_COL:
		case KZ_SCA_DB_SUBS_RECORD_ROUTE_COL:
		case KZ_SCA_DB_SUBS_ENDPOINT_ID_COL:
		case KZ_SCA_DB_SUBS_SOCKET_INFO_COL:
		case KZ_SCA_DB_SUBS_USER_AGENT_COL:
			((str *)column_value)->s = row_values[column].val.str_val.s;
			((str *)column_value)->len = row_values[column].val.str_val.len; //strlen(((str *)column_value)->s);
			break;

		case KZ_SCA_DB_SUBS_EXPIRES_COL:
			*((time_t *)column_value) = row_values[column].val.time_val;
			break;

		case KZ_SCA_DB_SUBS_EVENT_COL:
		case KZ_SCA_DB_SUBS_STATE_COL:
		case KZ_SCA_DB_SUBS_NOTIFY_CSEQ_COL:
		case KZ_SCA_DB_SUBS_SUBSCRIBE_CSEQ_COL:
		case KZ_SCA_DB_SUBS_SERVER_ID_COL:
			*((int *)column_value) = row_values[column].val.int_val;
			break;

		default:
			column_value = NULL;
	}
}

void kz_sca_db_subscriptions_set_value_for_column(int column, db_val_t *row_values, void *column_value)
{
	assert(column >= 0 && column < KZ_SCA_DB_SUBS_BOUNDARY);
	assert(column_value != NULL);
	assert(row_values != NULL);

	switch(column) {
		case KZ_SCA_DB_SUBS_SUBSCRIBER_COL:
		case KZ_SCA_DB_SUBS_AOR_COL:
		case KZ_SCA_DB_SUBS_CALL_ID_COL:
		case KZ_SCA_DB_SUBS_FROM_TAG_COL:
		case KZ_SCA_DB_SUBS_TO_TAG_COL:
		case KZ_SCA_DB_SUBS_RECORD_ROUTE_COL:
		case KZ_SCA_DB_SUBS_ENDPOINT_ID_COL:
		case KZ_SCA_DB_SUBS_SOCKET_INFO_COL:
		case KZ_SCA_DB_SUBS_USER_AGENT_COL:
			row_values[column].val.str_val = *((str *)column_value);
			row_values[column].type = DB1_STR;
			row_values[column].nul = 0;
			break;

		case KZ_SCA_DB_SUBS_EXPIRES_COL:
			row_values[column].val.int_val = (int)(*((time_t *)column_value));
			row_values[column].type = DB1_INT;
			row_values[column].nul = 0;
			break;

		case KZ_SCA_DB_SUBS_APP_IDX_COL:
			// for now, don't save appearance index associated with subscriber
			row_values[column].val.int_val = 0;
			row_values[column].type = DB1_INT;
			row_values[column].nul = 0;
			break;

		default:
			LM_WARN("kz_sca_db_subscriptions_set_value_for_column: unrecognized column index %d, treating as INT\n", column);
			// fall through

		case KZ_SCA_DB_SUBS_EVENT_COL:
		case KZ_SCA_DB_SUBS_STATE_COL:
		case KZ_SCA_DB_SUBS_NOTIFY_CSEQ_COL:
		case KZ_SCA_DB_SUBS_SUBSCRIBE_CSEQ_COL:
		case KZ_SCA_DB_SUBS_SERVER_ID_COL:
			row_values[column].val.int_val = *((int *)column_value);
			row_values[column].type = DB1_INT;
			row_values[column].nul = 0;
			break;
	}
}

str **kz_sca_db_subscriptions_columns(void)
{
	static str *subs_columns[] = {
		(str *)&KZ_SCA_DB_SUBSCRIBER_COL_NAME,
		(str *)&KZ_SCA_DB_AOR_COL_NAME,
		(str *)&KZ_SCA_DB_EVENT_COL_NAME,
		(str *)&KZ_SCA_DB_EXPIRES_COL_NAME,
		(str *)&KZ_SCA_DB_STATE_COL_NAME,
		(str *)&KZ_SCA_DB_APP_IDX_COL_NAME,
		(str *)&KZ_SCA_DB_CALL_ID_COL_NAME,
		(str *)&KZ_SCA_DB_FROM_TAG_COL_NAME,
		(str *)&KZ_SCA_DB_TO_TAG_COL_NAME,
		(str *)&KZ_SCA_DB_RECORD_ROUTE_COL_NAME,
		(str *)&KZ_SCA_DB_NOTIFY_CSEQ_COL_NAME,
		(str *)&KZ_SCA_DB_SUBSCRIBE_CSEQ_COL_NAME,
		(str *)&KZ_SCA_DB_SERVER_ID_COL_NAME,
		(str *)&KZ_SCA_DB_ENDPOINT_ID_COL_NAME,
		(str *)&KZ_SCA_DB_SOCKET_INFO_COL_NAME,
		(str *)&KZ_SCA_DB_USER_AGENT_COL_NAME,
		NULL
	};

	return (subs_columns);
}

db1_con_t *kz_sca_db_get_connection(void)
{
	assert(sca && sca->cfg->db_url);
	assert(sca->db_api && sca->db_api->init);

	if(kz_sca_db_con == NULL) {
		kz_sca_db_con = sca->db_api->init(sca->cfg->db_url);
		// catch connection error in caller
	}

	return (kz_sca_db_con);
}

void kz_sca_db_disconnect(void)
{
	if(kz_sca_db_con != NULL) {
		sca->db_api->close(kz_sca_db_con);
		kz_sca_db_con = NULL;
	}
}
