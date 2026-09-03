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

#include "kz_sca.h"
#include "kz_sca_appearance.h"
#include "kz_sca_call_info.h"
#include "kz_sca_event.h"
#include "kz_sca_notify.h"
#include "kz_sca_util.h"

#include "../../modules/tm/tm_load.h"

const str KZ_SCA_METHOD_NOTIFY = STR_STATIC_INIT("NOTIFY");


kz_sca_subscription_t *_sub_notify = NULL;

static int kz_pv_get_strval_null(struct sip_msg *msg, pv_param_t *param, pv_value_t *res, str *sval)
{
	if(res==NULL)
		return -1;

	if (!sval) return pv_get_null(msg, param, res);
	if (sval->s == NULL) return pv_get_null(msg, param, res);
	if (!sval->len) return pv_get_null(msg, param, res);

	res->rs = *sval;
	res->flags = PV_VAL_STR;
	return 0;
}

static str* kz_str_dup(str* src)
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


static void kz_sca_notify_reply_cb(tm_cell_t *t, int cb_type, struct tmcb_params *cbp)
{
	struct sip_msg *notify_reply = NULL;
	to_body_t * to = NULL;
	str to_aor = STR_NULL;
	str call_id = STR_NULL;
	str *contact_uri = NULL;
	str *endpoint_id = NULL;
	int remain = 0;

	if(cbp == NULL) {
		LM_ERR("Empty parameters passed to NOTIFY callback!\n");
		goto end;
	}

	endpoint_id = (str *)(*cbp->param);

	if((notify_reply = cbp->rpl) == NULL) {
		LM_ERR("Empty reply passed to NOTIFY callback!\n");
		goto end;
	}

	contact_uri = &t->uac[0].uri;

	if(notify_reply != FAKED_REPLY && REPLY_CLASS(notify_reply) == 2) {
		SCA_DBG("NOTIFY %.*s returned %d\n", STR_FMT(contact_uri), notify_reply->REPLY_STATUS);
		goto end;
	}

	to_aor = t->to_hdr;
	// t->to is the entire To header: "To: sip:....\r\n", so move to_aor.s ahead
	if(memcmp(to_aor.s, "To: ", strlen("To: ")) == 0) {
		to_aor.s += strlen("To: ");
		to_aor.len -= strlen("To: ");
	}

	to = pkg_malloc(sizeof(to_body_t));
	if(to == NULL) {
		PKG_MEM_ERROR;
		goto end;
	}

	if(kz_sca_parse_uri(to, &to_aor) < 0) {
		LM_ERR("Failed to parse aor from to header %.*s\n", STR_FMT(&to_aor));
		goto end;
	}

	to_aor = to->uri;

	call_id = t->callid_hdr;
	// t->callid is the entire Call-ID header: "Call-ID: ....\r\n", so move call_id.s ahead
	if(memcmp(call_id.s, "Call-ID: ", strlen("Call-ID: ")) == 0) {
		call_id.s += strlen("Call-ID: ");
		call_id.len -= strlen("Call-ID: ");
		call_id.len -= CRLF_LEN;
	}

	// after this, we've either gotten an error from the client, or a faked
	// reply from the proxy. remove the subscription in either case. it's
	// possible the client will return 481 (no such transaction), but that's
	// still grounds for us to remove the subscription, since the dialog
	// we have associated with the subscription is no longer valid.
	if(notify_reply == FAKED_REPLY) {
		SCA_DBG("NOTIFY %.*s resulted in FAKED_REPLY from proxy for aor %.*s : failed to deliver NOTIFY to client\n", STR_FMT(contact_uri), STR_FMT(&to_aor));
	} else {
		SCA_DBG("NOTIFY %.*s returned %d %.*s removing call-info subscription for %.*s (%.*s)\n", STR_FMT(contact_uri), notify_reply->REPLY_STATUS, STR_FMT(&notify_reply->first_line.u.reply.reason), STR_FMT(&to_aor), STR_FMT(endpoint_id));
	}

	if((remain = kz_sca_subscription_delete_subscriber_for_event_call_id(sca, endpoint_id, &KZ_SCA_EVENT_NAME_CALL_INFO, &to_aor, &call_id)) < 0) {
		LM_ERR("Failed to delete %.*s (%.*s) %.*s subscription\n", STR_FMT(&to_aor), STR_FMT(endpoint_id), STR_FMT(&KZ_SCA_EVENT_NAME_CALL_INFO));
	} else if(remain == 0) {
		kz_sca_appearance_owner_release_all(&to_aor, endpoint_id);
	}

end:
	if (endpoint_id) {
		shm_free(endpoint_id);
	}

	if(to) {
		free_to(to);
	}

}

static dlg_t *kz_sca_notify_dlg_for_subscription(kz_sca_subscription_t *sub)
{
	dlg_t *dlg = NULL;

	dlg = (dlg_t *)pkg_malloc(sizeof(dlg_t));
	if(dlg == NULL) {
		LM_ERR("pkg_malloc dlg_t for %.*s failed: out of memory\n", STR_FMT(&sub->subscriber));
		goto error;
	}
	memset(dlg, 0, sizeof(dlg_t));

	dlg->loc_seq.value = sub->dialog.notify_cseq;
	dlg->loc_seq.is_set = 1;

	dlg->id.call_id = sub->dialog.call_id;
	dlg->id.rem_tag = sub->dialog.from_tag;
	dlg->id.loc_tag = sub->dialog.to_tag;

	// RURI
	dlg->rem_target = sub->subscriber;

	// To and From URIs are both the KZ_SCA AoR in an KZ_SCA NOTIFY
	dlg->loc_uri = sub->target_aor;
	dlg->rem_uri = sub->target_aor;

	// restore route
	if(!KZ_SCA_STR_EMPTY(&sub->rr)) {
		if(parse_rr_body(sub->rr.s, sub->rr.len, &dlg->route_set) < 0) {
			LM_ERR("kz_sca_notify_dlg_for_subscription: failed to parse %.*s subscription's Record-Route info\n", STR_FMT(&sub->subscriber));
			goto error;
		}
	}

	if(sub->sockinfo.len) {
		int port, proto;
		str host;
		char *tmp;
		if((tmp = as_asciiz(&sub->sockinfo)) == NULL) {
			LM_ERR("no pkg memory left\n");
			goto error;
		}
		if(parse_phostport(tmp, &host.s, &host.len, &port, &proto)) {
			LM_ERR("bad sockinfo string\n");
			pkg_free(tmp);
			goto error;
		}
		dlg->send_sock = grep_sock_info(&host, (unsigned short)port, (unsigned short)proto);
		pkg_free(tmp);
	}

	// the dialog state in an KZ_SCA NOTIFY should always be confirmed,
	// since we generated the dialog to-tag in our response to the
	// subscriber's SUBSCRIBE request.
	dlg->state = DLG_CONFIRMED;

	return (dlg);

error:
	if(dlg != NULL) {
		pkg_free(dlg);
	}

	return (NULL);
}

static int kz_sca_notify_append_subscription_state_header(kz_sca_subscription_t *sub, char *hdrbuf, int maxlen)
{
	str state_str = STR_NULL;
	int len, total = 0;
	int ttl = sub->expires - time(NULL);

	if(ttl < 0) {
		ttl = 0;
	}

	kz_sca_subscription_state_to_str(sub->state, &state_str);
	len = snprintf(hdrbuf, maxlen, "Subscription-State: %s", state_str.s);
	if(len >= maxlen) {
		goto error;
	}
	total += len;

	if(ttl > 0) {
		len = snprintf(hdrbuf + total, maxlen - total, ";expires=%d", ttl);
		if(len >= maxlen) {
			goto error;
		}
		total += len;
	}

	len = snprintf(hdrbuf + total, maxlen - total, "%s", CRLF);
	total += len;

	return (total);

error:
	LM_ERR("Cannot append Subscription-State header: buffer too small\n");
	return (-1);
}

static int kz_sca_notify_append_contact_header(kz_sca_subscription_t *sub, char *hdrbuf, int maxlen)
{
	int len = strlen("Contact: ");
	str *orig = NULL;

	if(sca->cfg->server_address != NULL) {
		orig = sca->cfg->server_address;
	} else {
		orig = &sub->target_aor;
	}

	if(len + orig->len + strlen(CRLF) >= maxlen) {
		LM_ERR("Cannot append Contact header: buffer too small\n");
		return (-1);
	}

	memcpy(hdrbuf, "Contact: ", len);
	memcpy(hdrbuf + len, orig->s, orig->len);
	len += orig->len;
	memcpy(hdrbuf + len, CRLF, strlen(CRLF));
	len += strlen(CRLF);

	return (len);
}

static int kz_sca_notify_build_headers_from_info(str *hdrs, int max_hdrs_len, kz_sca_mod *scam, kz_sca_subscription_t *sub, int app_idx)
{
	int len = 0;

	assert(hdrs != NULL);

	len = kz_sca_notify_append_contact_header(sub, hdrs->s, max_hdrs_len);
	if(len < 0) {
		LM_ERR("Failed to add Contact header to %s NOTIFY for %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
		goto error;
	}
	hdrs->len = len;

	if(app_idx == KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY) {
		// add Call-Info header with appearance state
		if((len = kz_sca_call_info_build_header(scam, sub, hdrs->s + hdrs->len, max_hdrs_len - hdrs->len)) < 0) {
			LM_ERR("Failed to build Call-Info Headers for %s NOTIFY to %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
			goto error;
		}
	} else {
		// just add Call-Info header with single appearance index
		len = kz_sca_call_info_append_header_for_appearance_index(sub, app_idx, hdrs->s + hdrs->len, max_hdrs_len - hdrs->len);
		if(len < 0) {
			goto error;
		}
	}

	hdrs->len += len;

	len = kz_sca_event_append_header_for_type(sub->event, hdrs->s + hdrs->len, max_hdrs_len - hdrs->len);
	if(len < 0) {
		LM_ERR("Failed to add Event header to %s NOTIFY for %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
		goto error;
	}
	hdrs->len += len;

	len = kz_sca_notify_append_subscription_state_header(sub, hdrs->s + hdrs->len, max_hdrs_len - hdrs->len);
	if(len < 0) {
		LM_ERR("Failed to add Subscription-State header to %s NOTIFY for %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
		goto error;
	}
	hdrs->len += len;

	return (hdrs->len);

error:
	return (-1);
}

static int kz_sca_notify_subscriber_internal(kz_sca_mod *scam, kz_sca_subscription_t *sub, str *headers)
{
	uac_req_t request;
	dlg_t *dlg = NULL;
	str state_str = STR_NULL;
	int rc = -1;
	str* endpoint_id = NULL;

	kz_sca_appearance_state_to_str(sub->state, &state_str);
	SCA_DBG("KZ_SCA: NOTIFYing subscriber '%.*s' of event '%s' with a state of '%.*s' to index '%d'\n", STR_FMT(&sub->subscriber), kz_sca_event_name_from_type(sub->event), STR_FMT(&state_str), sub->index);

	dlg = kz_sca_notify_dlg_for_subscription(sub);
	if(dlg == NULL) {
		LM_ERR("Failed to create dlg_t for %s NOTIFY to %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
		goto done;
	}

	endpoint_id = kz_str_dup(&sub->endpoint_id);
	if(endpoint_id == NULL) {
		LM_ERR("Failed to create endpoint_id for %s NOTIFY to %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
		goto done;
	}

	_sub_notify	 = sub;

	set_uac_req(&request, (str *)&KZ_SCA_METHOD_NOTIFY, headers, NULL, dlg, TMCB_LOCAL_COMPLETED, kz_sca_notify_reply_cb, endpoint_id);
	rc = scam->tm_api->t_request_within(&request);
	if(rc < 0) {
		LM_ERR("Failed to send in-dialog %s NOTIFY to %.*s\n", kz_sca_event_name_from_type(sub->event), STR_FMT(&sub->subscriber));
	}
	// fall through, return rc from t_request_within

done:
	_sub_notify = NULL;

	if(dlg != NULL) {
		if(dlg->route_set != NULL) {
			free_rr(&dlg->route_set);
		}

		pkg_free(dlg);
	}

	return (rc);
}

#define KZ_SCA_HEADERS_MAX_LEN 4096
int kz_sca_notify_subscriber(kz_sca_mod *scam, kz_sca_subscription_t *sub, int app_idx)
{
	str headers = STR_NULL;
	char hdrbuf[KZ_SCA_HEADERS_MAX_LEN];

	SCA_DBG("NOTIFYing subscriber because of a SUBSCRIPTION request\n");

	headers.s = hdrbuf;
	if(kz_sca_notify_build_headers_from_info(&headers, sizeof(hdrbuf), scam, sub, app_idx) < 0) {
		LM_ERR("Failed to build NOTIFY headers\n");
		return (-1);
	}

	return (kz_sca_notify_subscriber_internal(scam, sub, &headers));
}

/*
 * send a call-info NOTIFY to all subscribers to a given KZ_SCA AoR.
 */
int kz_sca_notify_call_info_subscribers(kz_sca_mod *scam, str *subscription_aor)
{
	kz_sca_hash_slot *slot;
	kz_sca_hash_entry *e;
	kz_sca_subscription_t *sub;
	str headers = STR_NULL;
	str hash_key = STR_NULL;
	char hdrbuf[KZ_SCA_HEADERS_MAX_LEN];
	char keybuf[512];
	char *event_name;
	int slot_idx;
	int rc = -1;

	assert(scam->subscriptions != NULL);
	assert(!KZ_SCA_STR_EMPTY(subscription_aor));

	SCA_DBG("Notifying ALL subscribers of AOR %.*s due to a SUBSCRIPTION request\n", STR_FMT(subscription_aor));

	event_name = kz_sca_event_name_from_type(KZ_SCA_EVENT_TYPE_CALL_INFO);
	if(subscription_aor->len + strlen(event_name) >= sizeof(keybuf)) {
		LM_ERR("Hash key %.*s + %s is too long\n", STR_FMT(subscription_aor), event_name);
		return (-1);
	}
	hash_key.s = keybuf;
	KZ_SCA_STR_COPY(&hash_key, subscription_aor);
	KZ_SCA_STR_APPEND_CSTR(&hash_key, event_name);

	slot_idx = kz_sca_hash_table_index_for_key(scam->subscriptions, &hash_key);
	slot = kz_sca_hash_table_slot_for_index(scam->subscriptions, slot_idx);

	kz_sca_hash_table_lock_index(scam->subscriptions, slot_idx);

	for(e = slot->entries; e != NULL; e = e->next) {
		sub = (kz_sca_subscription_t *)e->value;
		if(!KZ_SCA_STR_EQ(subscription_aor, &sub->target_aor)) {
			continue;
		}

		if(headers.len == 0) {
			headers.s = hdrbuf;

			if(kz_sca_notify_build_headers_from_info(&headers, sizeof(hdrbuf), scam, sub, KZ_SCA_CALL_INFO_APPEARANCE_INDEX_ANY) < 0) {
				LM_ERR("Failed to build NOTIFY headers\n");
				goto done;
			}
		}

		// XXX would like this to be wrapped in one location
		sub->dialog.notify_cseq += 1;

		if(kz_sca_notify_subscriber_internal(scam, sub, &headers) < 0) {
			goto done;
		}
	}
	rc = 1;

done:
	kz_sca_hash_table_unlock_index(scam->subscriptions, slot_idx);

	return (rc);
}

/*
 * Map between $kz_subs(idxname) and kz_sca_subscription_t
 *
 * subscriber 1
 * aor 2
 * event 3
 * state 4
 * call_id 5
 * from_tag 6
 * to_tag 7
 * record_route 8
 * endpoint_id 9
 * sockinfo 10
 * user_agent 11
 */
int kz_sca_pv_parse_subs_name(pv_spec_p sp, str *in)
{
	if(sp == NULL || in == NULL || in->len <= 0)
		return -1;

	switch(in->len) {
		case 3:
			if(strncmp(in->s, "aor", 3) == 0) {
				sp->pvp.pvn.u.isname.name.n = 2;
			} else {
				goto error;
			};
			break;

		case 5:
			if(strncmp(in->s, "event", 5) == 0) {
				sp->pvp.pvn.u.isname.name.n = 3;
			} else if(strncmp(in->s, "state", 5) == 0) {
				sp->pvp.pvn.u.isname.name.n = 4;
			} else {
				goto error;
			};
			break;

		case 6:
			if(strncmp(in->s, "to_tag", 6) == 0) {
				sp->pvp.pvn.u.isname.name.n = 7;
			} else {
				goto error;
			};
			break;

		case 7:
			if(strncmp(in->s, "call_id", 7) == 0) {
				sp->pvp.pvn.u.isname.name.n = 5;
			} else {
				goto error;
			};
			break;

		case 8:
			if(strncmp(in->s, "from_tag", 8) == 0) {
				sp->pvp.pvn.u.isname.name.n = 6;
			} else if(strncmp(in->s, "sockinfo", 8) == 0) {
				sp->pvp.pvn.u.isname.name.n = 10;
			} else {
				goto error;
			};
			break;

		case 10:
			if(strncmp(in->s, "subscriber", 10) == 0) {
				sp->pvp.pvn.u.isname.name.n = 1;
			} else if(strncmp(in->s, "user_agent", 10) == 0) {
				sp->pvp.pvn.u.isname.name.n = 11;
			} else {
				goto error;
			};
			break;

		case 11:
			if(strncmp(in->s, "endpoint_id", 11) == 0) {
				sp->pvp.pvn.u.isname.name.n = 9;
			} else {
				goto error;
			};
			break;

		case 12:
			if(strncmp(in->s, "record_route", 12) == 0) {
				sp->pvp.pvn.u.isname.name.n = 8;
			} else {
				goto error;
			};
			break;

		default:
			goto error;
	}
	sp->pvp.pvn.type = PV_NAME_INTSTR;
	sp->pvp.pvn.u.isname.type = 0;

	return 0;

error:
	LM_ERR("unknown PV subscription name %.*s\n", in->len, in->s);
	return -1;
}

int kz_sca_pv_get_subs(sip_msg_t *msg, pv_param_t *param, pv_value_t *res)
{

	kz_sca_subscription_t *sub = _sub_notify;
	int ret = -1;
	str helper;
//	char buffer[1024];

	if(sub == NULL) {
		SCA_DBG("kz_subs called without subscrition.");
		return pv_get_null(msg, param, res);
	}
	if(param->pvn.u.isname.name.n == 1) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->subscriber);
	} else if(param->pvn.u.isname.name.n == 2) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->target_aor);
	} else if(param->pvn.u.isname.name.n == 3) {
		helper.s = kz_sca_event_name_from_type(sub->event);
		helper.len = strlen(helper.s);
		ret = pv_get_strval(msg, param, res, &helper);
	} else if(param->pvn.u.isname.name.n == 4) {
		kz_sca_subscription_state_to_str(sub->state, &helper);
		ret = pv_get_strval(msg, param, res, &helper);
	} else if(param->pvn.u.isname.name.n == 5) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->dialog.call_id);
	} else if(param->pvn.u.isname.name.n == 6) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->dialog.from_tag);
	} else if(param->pvn.u.isname.name.n == 7) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->dialog.to_tag);
	} else if(param->pvn.u.isname.name.n == 8) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->rr);
	} else if(param->pvn.u.isname.name.n == 9) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->endpoint_id);
	} else if(param->pvn.u.isname.name.n == 10) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->sockinfo);
	} else if(param->pvn.u.isname.name.n == 11) {
		ret = kz_pv_get_strval_null(msg, param, res, &sub->user_agent);
	}

	return ret;
}
