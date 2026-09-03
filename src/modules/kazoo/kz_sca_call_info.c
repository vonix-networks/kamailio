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
#include "kz_sca_dialog.h"
#include "kz_sca_event.h"
#include "kz_sca_notify.h"
#include "kz_sca_reply.h"
#include "kz_sca_subscribe.h"
#include "kz_sca_util.h"

const str KZ_SCA_CALL_INFO_HEADER_STR = STR_STATIC_INIT("Call-Info: ");
const str KZ_SCA_CALL_INFO_HEADER_NAME = STR_STATIC_INIT("Call-Info");

static int kz_sca_call_info_domain_from_uri(str *uri, str *domain)
{
	assert(!KZ_SCA_STR_EMPTY(uri));
	assert(domain != NULL);

	domain->s = memchr(uri->s, '@', uri->len);
	if(domain->s == NULL) {
		// may be a sip:domain URI
		domain->s = memchr(uri->s, ':', uri->len);
		if(domain->s == NULL) {
			LM_ERR("Bad URI %.*s\n", STR_FMT(uri));
			return (-1);
		}
	}
	domain->s++;

	domain->len = (uri->s + uri->len) - domain->s;
	// XXX handle :port in URI?

	return (domain->len);
}

static int kz_sca_call_info_header_length(int given_length)
{
	assert(given_length >= 0);

	// given_length is assumed to contain length of index string,
	// state string, and uri string, if any

	// "<sip:DOMAIN>;appearance-index=N;appearance-state=STATE"
	given_length += strlen("<sip:>=;=;");
	given_length += KZ_SCA_APPEARANCE_INDEX_STR.len;
	given_length += KZ_SCA_APPEARANCE_STATE_STR.len;

	return (given_length);
}

static int kz_sca_call_info_header_length_for_appearance(
		kz_sca_appearance_t *appearance, str *aor)
{
	int len = 0;
	str domain = STR_NULL;
	str state_str = STR_NULL;

	assert(aor != NULL);

	// get length of stringified index, since conversion's destructive
	(void)int2str(appearance->index, &len);

	kz_sca_appearance_state_to_str(appearance->state, &state_str);
	len += state_str.len;

	if(!KZ_SCA_STR_EMPTY(&appearance->uri)) {
		// +1 for ';', +1 for '=' between param name and value, +2 for quotes
		len += KZ_SCA_APPEARANCE_URI_STR.len + 1 + 1 + 2;
		len += appearance->uri.len;
	}

	if(kz_sca_call_info_domain_from_uri(aor, &domain) < 0) {
		return (-1);
	}
	len += domain.len;

	len += kz_sca_call_info_header_length(domain.len);

	return (len);
}

static int kz_sca_call_info_header_append_appearances(kz_sca_mod *scam, kz_sca_subscription_t *sub, char *hdrbuf, int maxlen)
{
	kz_sca_appearance_list *app_list;
	kz_sca_appearance_t *app;
	kz_sca_hash_slot *slot;
	str domain;
	str state_str;
	int slot_idx;
	int len = -1;
	int usedlen = -1;

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, &sub->target_aor);
	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);

	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, &sub->target_aor);
	if(app_list == NULL) {
		goto done;
	}

	usedlen = 0;
	for(app = app_list->appearances; app != NULL; app = app->next) {
		len = kz_sca_call_info_header_length_for_appearance(app, &sub->target_aor);
		if(len < 0) {
			goto done;
		}

		if((maxlen - len) < 0) {
			LM_ERR("Call-Info header for AoR %.*s is too long\n", STR_FMT(&sub->target_aor));
			goto done;
		}

		memcpy(hdrbuf, "<sip:", strlen("<sip:"));
		len = strlen("<sip:");

		if(kz_sca_call_info_domain_from_uri(&sub->target_aor, &domain) < 0) {
			LM_ERR("Call-Info domain from uri for AoR %.*s is invalid\n", STR_FMT(&sub->target_aor));
			goto done;
		}

		memcpy(hdrbuf + len, domain.s, domain.len);
		len += domain.len;

		kz_sca_appearance_state_to_str(app->state, &state_str);

		// state_str.s is a nul-terminated string literal
		len += snprintf(hdrbuf + len, maxlen - len, ">;appearance-index=%d;appearance-state=%s", app->index, state_str.s);

		if(!KZ_SCA_STR_EMPTY(&app->uri)) {
			hdrbuf[len] = ';';
			len += 1;

			memcpy(hdrbuf + len, KZ_SCA_APPEARANCE_URI_STR.s, KZ_SCA_APPEARANCE_URI_STR.len);
			len += KZ_SCA_APPEARANCE_URI_STR.len;

			hdrbuf[len] = '=';
			len += 1;

			hdrbuf[len] = '"';
			len += 1;

			memcpy(hdrbuf + len, app->uri.s, app->uri.len);
			len += app->uri.len;

			hdrbuf[len] = '"';
			len += 1;
		}

		if(app->next) {
			memcpy(hdrbuf + len, ",", 1);
			len++;
		}

		maxlen -= len;
		hdrbuf += len;

		usedlen += len;
	}

done:
	kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);

	return (usedlen);
}

static int kz_sca_call_info_build_idle_value(kz_sca_mod *scam, str *aor, char *hdrbuf, int maxlen)
{
	str idle_domain = STR_NULL;
	int len;

	if(kz_sca_call_info_domain_from_uri(aor, &idle_domain) < 0) {
		LM_ERR("Failed to extract domain from %.*s for idle domain\n", STR_FMT(aor));
		return (-1);
	}

	// the KZ_SCA_APPEARANCE_ strs' s member are literal C strings
	len = snprintf(hdrbuf, maxlen, "<sip:%.*s>;%s=*;%s=%s%s", STR_FMT(&idle_domain), KZ_SCA_APPEARANCE_INDEX_STR.s, KZ_SCA_APPEARANCE_STATE_STR.s, KZ_SCA_APPEARANCE_STATE_STR_IDLE.s, CRLF);
	if(len >= maxlen) {
		LM_ERR("Failed to add idle appearance: Call-Info header too long\n");
		len = -1;
		// snprintf can also return negative. we catch that in the caller.
	}

	return (len);
}

int kz_sca_call_info_build_header(kz_sca_mod *scam, kz_sca_subscription_t *sub, char *hdrbuf, int maxlen)
{
	// we send one Call-Info header, appearances separated by commas
	int len;
	int usedlen = KZ_SCA_CALL_INFO_HEADER_STR.len;

	// begin with "Call-Info: "
	memcpy(hdrbuf, KZ_SCA_CALL_INFO_HEADER_STR.s, KZ_SCA_CALL_INFO_HEADER_STR.len);

	len = kz_sca_call_info_header_append_appearances(scam, sub, hdrbuf + usedlen, maxlen - usedlen);
	usedlen += len;
	if(usedlen > KZ_SCA_CALL_INFO_HEADER_STR.len) {
		// we added an indexed appearance, append a comma
		memcpy(hdrbuf + usedlen, ",", 1);
		usedlen++;
	}

	// line-seize NOTIFYs will contain only the seized appearance index
	if(sub->event != KZ_SCA_EVENT_TYPE_LINE_SEIZE) {
		// if not all appearances in use, add *-index idle
		len = kz_sca_call_info_build_idle_value(scam, &sub->target_aor, hdrbuf + usedlen, maxlen - usedlen);
		if(len < 0 || len + usedlen >= maxlen) {
			LM_ERR("Cannot build idle Call-Info value: buffer too small\n");
			return (-1);
		}
		usedlen += len;
	}

	return (usedlen);
}

int kz_sca_call_info_append_header_for_appearance_index(kz_sca_subscription_t *sub, int appearance_index, char *hdrbuf, int maxlen)
{
	str domain = STR_NULL;
	char *app_index_p = NULL;
	int len = 0, idx_len;

	memcpy(hdrbuf, KZ_SCA_CALL_INFO_HEADER_STR.s, KZ_SCA_CALL_INFO_HEADER_STR.len);
	len += KZ_SCA_CALL_INFO_HEADER_STR.len;
	if(len >= maxlen) {
		goto error;
	}

	memcpy(hdrbuf + len, "<sip:", strlen("<sip:"));
	len += strlen("<sip:");
	if(len >= maxlen) {
		goto error;
	}

	kz_sca_call_info_domain_from_uri(&sub->target_aor, &domain);
	memcpy(hdrbuf + len, domain.s, domain.len);
	len += domain.len;
	if(len >= maxlen) {
		goto error;
	}

	memcpy(hdrbuf + len, ">;appearance-index=", strlen(">;appearance-index="));
	len += strlen(">;appearance-index=");
	if(len >= maxlen) {
		goto error;
	}

	app_index_p = int2str(appearance_index, &idx_len);
	memcpy(hdrbuf + len, app_index_p, idx_len);
	len += idx_len;
	if(len >= maxlen) {
		goto error;
	}

	memcpy(hdrbuf + len, CRLF, CRLF_LEN);
	len += CRLF_LEN;
	if(len >= maxlen) {
		goto error;
	}

	return (len);

error:
	LM_ERR("Failed to append Call-Info header for %.*s appearance index %d\n", STR_FMT(&sub->subscriber), appearance_index);
	return (-1);
}

int kz_sca_call_info_body_parse(str *hdr_body, kz_sca_call_info *call_info)
{
	str s = STR_NULL;
	char *p;
	char *semi;
	int len;

	assert(call_info != NULL);

	if(KZ_SCA_STR_EMPTY(hdr_body)) {
		LM_ERR("Call-Info header body is empty\n");
		return (-1);
	}

	call_info->kz_sca_uri.s = NULL;
	call_info->kz_sca_uri.len = 0;
	call_info->index = -1;
	call_info->state = KZ_SCA_APPEARANCE_STATE_UNKNOWN;
	call_info->uri.s = NULL;
	call_info->uri.len = 0;

	p = hdr_body->s;
	if(memcmp(p, "<sip:", strlen("<sip:")) != 0) {
		LM_ERR("Bad Call-Info header body: must begin with \"<sip:\"\n");
		return (-1);
	}
	// +5 == strlen("<sip:")
	semi = memchr(p + 5, ';', hdr_body->len);
	if(semi == NULL) {
		LM_ERR("Bad Call-Info header body: missing ';' between uri and %.*s\n", STR_FMT(&KZ_SCA_APPEARANCE_INDEX_STR));
		return (-1);
	}
	if(*(semi - 1) != '>') {
		LM_ERR("Bad Call-Info header body: KZ_SCA URI missing '>' terminator\n");
		return (-1);
	}

	call_info->kz_sca_uri.s = p;
	call_info->kz_sca_uri.len = semi - p;

	p = semi;
	p++;
	if(memcmp(p, KZ_SCA_APPEARANCE_INDEX_STR.s, KZ_SCA_APPEARANCE_INDEX_STR.len) != 0) {
		LM_ERR("Bad Call-Info header body: does not begin with %.*s\n", STR_FMT(&KZ_SCA_APPEARANCE_INDEX_STR));
		return (-1);
	}

	p += KZ_SCA_APPEARANCE_INDEX_STR.len;
	if(*p != '=') {
		LM_ERR("Bad Call-Info header body: missing '=' after %.*s\n", STR_FMT(&KZ_SCA_APPEARANCE_INDEX_STR));
		return (-1);
	}

	p++;
	len = (hdr_body->s + hdr_body->len) - p;
	semi = memchr(p, ';', len);
	if(semi != NULL) {
		len = semi - p;
	}
	s.s = p;
	s.len = len;

	if(str2int(&s, (unsigned int *)&call_info->index) != 0) {
		LM_ERR("Bad Call-Info header: failed to convert %.*s %.*s to an integer\n", STR_FMT(&KZ_SCA_APPEARANCE_INDEX_STR), STR_FMT(&s));
		return (-1);
	}

	if(semi == NULL) {
		// Call-Info header only contained an appearance-index
		goto done;
	}

	// advance appearance-index value + semi-colon
	p += (len + 1);
	if(memcmp(p, KZ_SCA_APPEARANCE_STATE_STR.s, KZ_SCA_APPEARANCE_STATE_STR.len) != 0) {
		LM_ERR("Bad Call-Info header: missing %.*s\n", STR_FMT(&KZ_SCA_APPEARANCE_STATE_STR));
		return (-1);
	}

	p += KZ_SCA_APPEARANCE_STATE_STR.len;
	if(*p != '=') {
		LM_ERR("Bad Call-Info header body: missing '=' after %.*s\n", STR_FMT(&KZ_SCA_APPEARANCE_STATE_STR));
		return (-1);
	}

	p++;
	len = (hdr_body->s + hdr_body->len) - p;
	semi = memchr(p, ';', len);
	if(semi != NULL) {
		len = semi - p;
	}
	s.s = p;
	s.len = len;

	call_info->state = kz_sca_appearance_state_from_str(&s);
	if(call_info->state == KZ_SCA_APPEARANCE_STATE_UNKNOWN) {
		LM_ERR("Bad Call-Info header: unrecognized state \"%.*s\"\n", STR_FMT(&s));
		return (-1);
	}

	if(semi == NULL) {
		// Call-Info header only had appearance-index & appearance-state
		goto done;
	}

	// advance length of state + semi-colon
	p += (len + 1);
	if(memcmp(p, KZ_SCA_APPEARANCE_URI_STR.s, KZ_SCA_APPEARANCE_URI_STR.len) != 0) {
		LM_ERR("Bad Call-Info header: missing %.*s\n", STR_FMT(&KZ_SCA_APPEARANCE_URI_STR));
		return (-1);
	}

	p += KZ_SCA_APPEARANCE_URI_STR.len;
	if(*p != '=') {
		LM_ERR("Bad Call-Info header: missing '=' after %.*s\n", STR_FMT(&KZ_SCA_APPEARANCE_URI_STR));
		return (-1);
	}

	p++;
	call_info->uri.s = p;
	call_info->uri.len = (hdr_body->s + hdr_body->len) - p;

	if(KZ_SCA_STR_EMPTY(&call_info->uri)) {
		LM_ERR("Bad Call-Info header: empty %.*s\n", STR_FMT(&KZ_SCA_APPEARANCE_URI_STR));
		return (-1);
	}

done:
	return (0);
}

/*
 * return codes:
 *	-1: error, failed to remove call-info header.
 *	 0: no call-info headers found.
 *     >=1: removed 1 or more call-info headers.
 */
/*
static int kz_sca_call_info_header_remove(sip_msg_t *msg)
{
	hdr_field_t *hdr;
	struct lump *ci_hdr_lump;
	int rc = 0;

	// all headers must be parsed before using del_lump
	if(parse_headers(msg, HDR_EOH_F, 0) < 0) {
		LM_ERR("Failed to parse_headers\n");
		return (-1);
	}
	for(hdr = get_hdr(msg, HDR_CALLINFO_T); hdr; hdr = next_sibling_hdr(hdr)) {
		// del_lump takes packet, offset, lump length, & hdr type
		ci_hdr_lump =
				del_lump(msg, hdr->name.s - msg->buf, hdr->len, HDR_CALLINFO_T);
		if(ci_hdr_lump == NULL) {
			LM_ERR("Failed to del_lump Call-Info header\n");
			rc = -1;
			break;
		}

		rc++;
	}

	return (rc);
}
*/

//static int
int kz_sca_call_info_seize_held_call(sip_msg_t *msg, kz_sca_call_info *call_info, struct to_body *from, struct to_body *to, str *from_aor, str *to_aor, str *contact_uri)
{
	kz_sca_appearance_t *app;
	struct lump *anchor;
	str callee_aor = STR_NULL;
	str replaces_hdr = STR_NULL;
	str prev_callid = STR_NULL;
	str prev_totag = STR_NULL;
	char tagbuf[1024];
	char callee_buf[1024];
	int slot_idx = -1;
	int rc = -1;

	SCA_DBG("From-AOR:%.*s To-AOR:%.*s From-URI:<%.*s> To-URI:<%.*s> Contact: <%.*s> Call-Info: appearance-index=%d\n", STR_FMT(from_aor), STR_FMT(to_aor), STR_FMT(&from->uri), STR_FMT(&to->uri), STR_FMT(contact_uri), call_info->index);

	slot_idx = kz_sca_hash_table_index_for_key(sca->appearances, from_aor);
	kz_sca_hash_table_lock_index(sca->appearances, slot_idx);

	app = kz_sca_appearance_for_index_unsafe(sca, from_aor, call_info->index, slot_idx);
	if(app == NULL) {
		LM_ERR("kz_sca_call_info_seize_held_call: no active appearances for %.*s\n", STR_FMT(from_aor));
		goto done;
	}

	if(app->state == KZ_SCA_APPEARANCE_STATE_HELD_PRIVATE) {
		// spec calls for "403 Forbidden" when non-owner tries to
		// seize a privately held call. if we get here, there's no
		// to-tag in the INVITE, meaning this isn't a reINVITE
		// from the owner to take the call off private hold.
		KZ_SCA_REPLY_ERROR(sca, 403, "Forbidden - private call", msg);

		// rc bubbles up to script. 0 tells script to stop processing.
		rc = 0;
		goto done;
	}

	SCA_DBG("kz_sca_call_info_seize_held_call: seizing %.*s index %d, callee %.*s\n", STR_FMT(from_aor), app->index, STR_FMT(&app->callee));

	// rewrite the RURI to use the callee in this KZ_SCA dialog
	if(msg->new_uri.s) {
		// someone already rewrote the URI. shouldn't happen, but we have
		// to watch for it. log our overwriting of it.
		SCA_DBG("KZ_SCA caller retrieving held call, but RURI was already rewritten as %.*s. Overwriting with %.*s.\n", STR_FMT(&msg->new_uri), STR_FMT(&app->callee));

		pkg_free(msg->new_uri.s);
		msg->new_uri.s = NULL;
		msg->new_uri.len = 0;
	}

	// msg->new_uri.s is free'd when transaction is torn down
	msg->new_uri.s = (char *)pkg_malloc(app->callee.len);
	if(msg->new_uri.s == NULL) {
		LM_ERR("kz_sca_call_info_seize_held_call: pkg_malloc new RURI %.*s failed\n", STR_FMT(&app->callee));
		goto done;
	}
	KZ_SCA_STR_COPY(&msg->new_uri, &app->callee);

	{
		int idx;

		for(idx = 0; get_sip_branch(idx) != NULL; idx++)
			;

		for(; idx >= 0; idx--) {
			drop_sip_branch(idx);
		}
	}

	// must reset to avoid using cached parsed RURI
	msg->parsed_uri_ok = 0;
	ruri_mark_new();

	// store the previous dialog's tags for lookup of the callee
	prev_callid.s = tagbuf;
	KZ_SCA_STR_COPY(&prev_callid, &app->dialog.call_id);

	prev_totag.s = tagbuf + prev_callid.len;
	KZ_SCA_STR_COPY(&prev_totag, &app->dialog.to_tag);

	// pkg_malloc's replaces_hdr.s, which is free'd if added as lump
	if(kz_sca_dialog_create_replaces_header(&app->dialog, &replaces_hdr) < 0) {
		LM_ERR("kz_sca_call_info_seize_held_call: failed to create Replaces header for %.*s from dialog %.*s (%.*s / %.*s)\n", STR_FMT(from_aor), STR_FMT(&app->dialog.id), STR_FMT(&app->dialog.from_tag), STR_FMT(&app->dialog.to_tag));
		goto done;
	}

	// store the callee's username for lookup of the callee by AoR
	callee_aor.s = callee_buf;
	if(kz_sca_uri_build_aor(&callee_aor, sizeof(callee_buf), &app->callee, from_aor) < 0) {
		LM_ERR("kz_sca_call_info_seize_held_call: failed to create To AoR from %.*s and %.*s\n", STR_FMT(&app->callee), STR_FMT(from_aor));
		pkg_free(replaces_hdr.s);
		goto done;
	}

	// all headers must be parsed before using lump functions
	if(parse_headers(msg, HDR_EOH_F, 0) < 0) {
		LM_ERR("Failed to parse_headers\n");
		goto done;
	}

	anchor = anchor_lump(msg, msg->eoh - msg->buf, 0, HDR_OTHER_T);
	if(anchor == NULL) {
		LM_ERR("Failed to anchor lump\n");
		goto done;
	}

	// append the Replaces header before the sdp body
	if(insert_new_lump_before(anchor, replaces_hdr.s, replaces_hdr.len, HDR_OTHER_T) == NULL) {
		LM_ERR("Failed to add Replaces header %.*s\n", STR_FMT(&replaces_hdr));
		pkg_free(replaces_hdr.s);
		goto done;
	}
	// RFC 3891 (Replaces header) suggests, but does not require, that the
	// UAS establish the dialog with the UAC replacing the existing dialog
	// before sending the BYE to the original UAC. Polycom handsets appear
	// to send the BYE to the original UAC first, so we save the pending
	// owner here. if the 200 OK arrives first, we update the owner and
	// dialog there. otherwise, we catch this in the 200 OK to the BYE
	// sent by the line being replaced.
	//
	// if the reINVITE to seize the held line fails for some reason,
	// we restore the original owner and dialog.
	if(kz_sca_appearance_update_owner_unsafe(app, contact_uri) < 0) {
		LM_ERR("kz_sca_call_info_seize_held_call: failed to update owner\n");
		pkg_free(replaces_hdr.s);
		goto done;
	}

	if(kz_sca_appearance_update_dialog_unsafe(app, &msg->callid->body, &from->tag_value, &to->tag_value) < 0) {
		LM_ERR("kz_sca_call_info_seize_held_call: failed to update dialog\n");
		goto done;
	}

	app->flags |= KZ_SCA_APPEARANCE_FLAG_OWNER_PENDING;
	kz_sca_appearance_update_state_unsafe(app, KZ_SCA_APPEARANCE_STATE_ACTIVE);

	kz_sca_hash_table_unlock_index(sca->appearances, slot_idx);
	slot_idx = -1;

	if(callee_aor.s != NULL && callee_aor.len > 0) {
		if(kz_sca_uri_lock_if_shared_appearance(sca, &callee_aor, &slot_idx)) {
			app = kz_sca_appearance_for_tags_unsafe(sca, &callee_aor, &prev_callid, &prev_totag, NULL, slot_idx);
			if(app == NULL) {
				LM_ERR("kz_sca_call_info_seize_held_call: failed to find appearance of %.*s with dialog %.*s;%.*s\n", STR_FMT(&callee_aor), STR_FMT(&prev_callid), STR_FMT(&prev_totag));
				goto done;
			}

			app->flags |= KZ_SCA_APPEARANCE_FLAG_CALLEE_PENDING;

			if(kz_sca_appearance_update_callee_unsafe(app, contact_uri) < 0) {
				LM_ERR("kz_sca_call_info_seize_held_call: failed to update callee\n");
				goto done;
			}
			if(kz_sca_appearance_update_dialog_unsafe(app, &msg->callid->body, &to->tag_value, &from->tag_value) < 0) {
				LM_ERR("kz_sca_call_info_seize_held_call: failed to update dialog\n");
				goto done;
			}
		}
	}

	rc = 1;

done:
	if(slot_idx >= 0) {
		kz_sca_hash_table_unlock_index(sca->appearances, slot_idx);
	}

	return (rc);
}

