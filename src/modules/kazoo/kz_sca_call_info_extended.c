#include "kz_sca_common.h"

#include <assert.h>

#include "kz_sca.h"
#include "kz_sca_appearance.h"
#include "kz_sca_dialog.h"
#include "kz_sca_event.h"
#include "kz_sca_notify.h"
#include "kz_sca_reply.h"
#include "kz_sca_subscribe.h"
#include "kz_sca_util.h"
#include "kz_sca_call_info.h"
#include "kz_sca_params.h"

const char * kz_event_payload();

extern str kz_db_url;
extern db1_con_t *kz_pa_db;
extern db_func_t kz_pa_dbf;

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


const char * kz_sca_presence_direction_to_string(kz_sca_presence_direction_t direction)
{
	switch(direction)
	{
		case KZ_PRESENCE_DIRECTION_INITIATOR:
			return "initiator";

		case KZ_PRESENCE_DIRECTION_RECIPIENT:
			return "recipient";

		default:
			break;
	}

	return "none";

}

void kz_sca_presence_direction_to_str(kz_sca_presence_direction_t direction, str *direction_str)
{
	assert(direction_str != NULL);

	direction_str->s = (char *) kz_sca_presence_direction_to_string(direction);
	direction_str->len = strlen(direction_str->s);
}


kz_sca_presence_direction_t kz_sca_presence_direction_from_string(const char * direction)
{
	if (!strcmp(direction, "initiator")) {
		return KZ_PRESENCE_DIRECTION_INITIATOR;
	} else if (!strcmp(direction, "recipient")) {
		return KZ_PRESENCE_DIRECTION_RECIPIENT;
	}

	return KZ_PRESENCE_DIRECTION_NONE;
}

kz_sca_presence_direction_t kz_sca_presence_direction_from_json(json_t *json)
{
	json_t * item = NULL;

	if ((item = kz_json_get_object(json, "Direction")) != NULL) {
		return kz_sca_presence_direction_from_string(json_object_get_string(item));
	}

	return KZ_PRESENCE_DIRECTION_NONE;
}


kz_sca_appearance_t *kz_sca_appearance_seized_index_unsafe(kz_sca_mod *scam, str *aor, str *endpoint_id, int slot_idx)
{
	kz_sca_appearance_list *app_list;
	kz_sca_appearance_t *app = NULL;
	kz_sca_hash_slot *slot;

	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, aor);
	if(app_list == NULL) {
		SCA_DBG("kz_sca_appearance_seize_index_unsafe: no appearance list for %.*s\n", STR_FMT(aor));
		goto done;
	}

	for(app = app_list->appearances; app != NULL; app = app->next) {
		if (app->state == KZ_SCA_APPEARANCE_STATE_SEIZED) {
			SCA_DBG("%.*s appearance index %i is seized, %.*s / %.*s \n", STR_FMT(aor), app->index, STR_FMT(&app->owner), STR_FMT(endpoint_id));
			if (KZ_SCA_STR_EQ(&app->owner, endpoint_id)) {
				break;
			}
		} else {
			SCA_DBG("%.*s appearance index %i state %s is seized, %.*s / %.*s \n", STR_FMT(aor), app->index, kz_sca_appearance_state_to_string(app->state), STR_FMT(&app->owner), STR_FMT(endpoint_id));
		}
	}

done:

	return (app);
}

kz_sca_appearance_t * kz_sca_appearance_for_dialog_unsafe_ex(kz_sca_mod *scam, str *aor, kz_sca_dialog *dialog, int slot_idx)
{
	kz_sca_appearance_list *app_list;
	kz_sca_appearance_t *app = NULL;
	kz_sca_hash_slot *slot;
	kz_sca_hash_entry *ent;

	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	app_list = NULL;
	for(ent = slot->entries; ent != NULL; ent = ent->next) {
		if(ent->compare(aor, ent->value) == 0) {
			app_list = (kz_sca_appearance_list *)ent->value;
			break;
		}
	}

	if(app_list == NULL) {
		SCA_DBG("No appearances for %.*s\n", STR_FMT(aor));
		return (NULL);
	}

	SCA_DBG("search for call_id[%.*s] from_tag[%.*s] to_tag[%.*s]\n", STR_FMT(&dialog->call_id), STR_FMT(&dialog->from_tag), STR_FMT(&dialog->to_tag));
	for(app = app_list->appearances; app != NULL; app = app->next) {
		SCA_DBG("app.call_id[%.*s] app.from_tag[%.*s] app.to_tag[%.*s]\n", STR_FMT(&app->dialog.call_id), STR_FMT(&app->dialog.from_tag), STR_FMT(&app->dialog.to_tag));

		if(KZ_SCA_STR_EQ(&app->dialog.call_id, &dialog->call_id)) {
			if(KZ_SCA_STR_EQ(&app->dialog.from_tag, &dialog->from_tag)) {
				if(KZ_SCA_STR_EQ(&app->dialog.to_tag, &dialog->to_tag)) {
					SCA_DBG("%.*s appearance-index %d perfect match\n", STR_FMT(aor), app->index);
				} else {
					SCA_DBG("%.*s appearance-index %d found from_tag\n", STR_FMT(aor), app->index);
				}
			} else {
				SCA_DBG("%.*s appearance-index %d found call_id\n", STR_FMT(aor), app->index);
			}
			break;
		}
	}

	return (app);
}

static str* kz_local_str_dup(str* src)
{
	char *dst_char = (char*)pkg_malloc(sizeof(str)+src->len+1);
	if (!dst_char) {
		LM_ERR("error allocating pkg memory for str");
		return NULL;
	}
	str* dst = (str*)dst_char;
	dst->s = dst_char+sizeof(str);

	memcpy(dst->s, src->s, src->len);
	dst->len = src->len;
	dst->s[dst->len] = '\0';
	return dst;
}

/*
static str* kz_local_str_dup_from_char(const char * src)
{
	int len = strlen(src);
	char *dst_char = (char*)pkg_malloc(sizeof(str)+len+1);
	if (!dst_char) {
		LM_ERR("error allocating pkg memory for str");
		return NULL;
	}
	str* dst = (str*)dst_char;
	dst->s = dst_char+sizeof(str);

	memcpy(dst->s, src, len);
	dst->len = len;
	dst->s[dst->len] = '\0';
	return dst;
}
*/

int kz_sca_verify_db_connection()
{
	if(kz_pa_db != NULL) return 0;

	kz_pa_db = kz_pa_dbf.init(&kz_db_url);
	if (!kz_pa_db)
	{
		LM_ERR("child %d: unsuccessful connecting to database\n", getpid());
		return 1;
	}
	return 0;
}

typedef str * call_info_target;
typedef call_info_target * call_info_targets;

call_info_targets kz_sca_call_info_targets(json_t *aliases, str* realm)
{
	db1_res_t* db_res = NULL;
	int i, n;
	str **list = NULL;
	str query;
	char *buffer = NULL;
	int buf_size, offset;

	if (!aliases || !json_object_is_type(aliases, json_type_array) || json_object_array_length(aliases) < 1)  {
		return NULL;
	}

	n = json_object_array_length(aliases);

	/* Calculate required buffer size */
	buf_size = 256; /* base query overhead */
	buf_size += cfg_get(kz_sca, kz_sca_cfg, aliases_view).len;
	buf_size += realm->len;
	for (i = 0; i < n; i++) {
		const char *alias = json_object_get_string(json_object_array_get_idx(aliases, i));
		if (alias) {
			buf_size += strlen(alias) + 6; /* quotes, comma, spaces */
		}
	}

	buffer = pkg_malloc(buf_size);
	if (buffer == NULL) {
		LM_ERR("kz_sca_call_info_targets: failed to pkg_malloc %d bytes for query\n", buf_size);
		return NULL;
	}

	offset = snprintf(buffer, buf_size, "select distinct aor from %.*s where realm = '%.*s' and alias in ( ", STR_FMT(&cfg_get(kz_sca, kz_sca_cfg, aliases_view)), STR_FMT(realm));
	for (i = 0; i < n; i++) {
		const char *alias = json_object_get_string(json_object_array_get_idx(aliases, i));
		if (alias == NULL) continue;
		if (i > 0) {
			offset += snprintf(buffer + offset, buf_size - offset, " , ");
		}
		offset += snprintf(buffer + offset, buf_size - offset, "'%s'", alias);
		if (offset >= buf_size) {
			LM_ERR("kz_sca_call_info_targets: query buffer overflow\n");
			pkg_free(buffer);
			return NULL;
		}
	}
	offset += snprintf(buffer + offset, buf_size - offset, ")");

	SCA_DBG("sql query => %s\n", buffer);

	query.s = buffer;
	query.len = offset;

	if(kz_pa_dbf.raw_query(kz_pa_db, &query, &db_res)!=0)
	{
		LM_ERR("cannot do the query\n");
		pkg_free(buffer);
		return NULL;
	}

	pkg_free(buffer);

	if(db_res==NULL || RES_ROW_N(db_res)<=0 || RES_COL_N(db_res)!=1)
	{
		SCA_DBG("no result after query\n");
		kz_pa_dbf.free_result(kz_pa_db, db_res);
		return NULL;
	}

	list = pkg_mallocxz(sizeof(call_info_target) * (RES_ROW_N(db_res) + 1));
	for(i=0; i < RES_ROW_N(db_res); i++)
	{
		db_val_t value = RES_ROWS(db_res)[i].values[0];
		// db_type_t type = value.type;
		SCA_DBG("sql row => %.*s\n", STR_FMT(&value.val.str_val));
		list[i] = kz_local_str_dup(&value.val.str_val);
	}

	kz_pa_dbf.free_result(kz_pa_db, db_res);

	return list;
}

void kz_sca_call_info_clean_targets(call_info_targets targets)
{
	call_info_targets xtargets = targets;
	call_info_target target;
	while ((target = *xtargets) != NULL) {
		pkg_free(target);
		xtargets++;
	}

	pkg_free(targets);

}

int kz_sca_dialog_build_from_json(kz_sca_dialog * dlg, int size, json_t * json)
{
	str from_tag = {0,0}, to_tag  = {0,0}, call_id = {0,0};
	json_t * item = NULL;

	if ((item = kz_json_get_object(json, "From-Tag")) != NULL) {
		from_tag.s = (char *) json_object_get_string(item);
		from_tag.len = strlen(from_tag.s);
	}

	if ((item = kz_json_get_object(json, "To-Tag")) != NULL) {
		to_tag.s = (char *) json_object_get_string(item);
		to_tag.len = strlen(to_tag.s);
	}

	if ((item = kz_json_get_object(json, "Call-ID")) != NULL) {
		call_id.s = (char *) json_object_get_string(item);
		call_id.len = strlen(call_id.s);
	}

	return kz_sca_dialog_build_from_tags(dlg, size, &call_id, &from_tag, &to_tag);
}

int kz_sca_extended_call_info_update(sip_msg_t *msg)
{
	json_t * json = NULL;
	json_t * call_info_json = NULL;
	json_t * item = NULL;
	const char *eventData = NULL;
	call_info_targets targets = NULL;
	call_info_target target = NULL;
	int rc = -1;

	kz_sca_dialog dialog;
	char dlg_buf[1024];

	int call_info_state = KZ_SCA_APPEARANCE_STATE_IDLE;

	str to_uri = {0, 0}, to_display_name = {0, 0};
	kz_sca_presence_direction_t direction;
	str endpoint_id = {0, 0}, switch_uri = {0, 0};
	str ccvs = {0, 0}, aliases = {0, 0}, call_interaction_id = {0, 0};
	str realm = {0, 0};

	eventData = kz_event_payload();
	if (eventData == NULL) {
		LM_ERR("event data is null, ignoring\n");
		goto end;
	}

	SCA_DBG("event data => %s\n", eventData);

	json = kz_json_parse(eventData);

	if (!json) {
		LM_ERR("event data cannot be parsed as json, ignoring\n");
		goto end;
	}

	if ((item = kz_json_get_field_object(json, cfg_get(kz_sca, kz_sca_cfg, realm_path).s)) == NULL) {
		SCA_DBG("no realm, ignoring\n");
		goto end;
	}

	realm.s = (char *) json_object_get_string(item);
	realm.len = strlen(realm.s);

	if ((item = kz_json_get_field_object(json, cfg_get(kz_sca, kz_sca_cfg, aliases_path).s)) == NULL) {
		SCA_DBG("no aliases, ignoring\n");
		goto end;
	}

	targets = kz_sca_call_info_targets(item, &realm);

	if (!targets) {
		SCA_DBG("no targets for aliases, ignoring\n");
		goto end;
	}

	aliases.s = (char *) json_object_get_string(item);
	aliases.len = strlen(aliases.s);


	if ((call_info_json = kz_json_get_object(json, "Call-Info")) == NULL) {
		SCA_DBG("no call-info ignoring\n");
		goto end;
	}

	dialog.id.s = dlg_buf;
	if (kz_sca_dialog_build_from_json(&dialog, sizeof(dlg_buf), json) < 0) {
		LM_ERR("Failed to build dialog from tags\n");
		goto end;
	}

	if ((item = kz_json_get_object(call_info_json, "State")) != NULL) {
		call_info_state = kz_sca_appearance_state_from_string(json_object_get_string(item));
	}

	if ((item = kz_json_get_object(json, "To")) != NULL) {
		to_uri.s = (char *) json_object_get_string(item);
		to_uri.len = strlen(to_uri.s);
	}

	if ((item = kz_json_get_object(json, "To-Display-Name")) != NULL) {
		to_display_name.s = (char *) json_object_get_string(item);
		to_display_name.len = strlen(to_display_name.s);
	}

	direction = kz_sca_presence_direction_from_json(json);

	if ((item = kz_json_get_field_object(json, "Custom-Channel-Vars.Authorizing-ID")) != NULL) {
		endpoint_id.s = (char *) json_object_get_string(item);
		endpoint_id.len = strlen(endpoint_id.s);
	}

	if ((item = kz_json_get_object(json, "Switch-URI")) != NULL) {
		switch_uri.s = (char *) json_object_get_string(item);
		switch_uri.len = strlen(switch_uri.s);
	}

	if ((item = kz_json_get_field_object(json, "Custom-Channel-Vars")) != NULL) {
		ccvs.s = (char *) json_object_get_string(item);
		ccvs.len = strlen(ccvs.s);
	}

	if ((item = kz_json_get_field_object(json, "Call-Interaction-ID")) != NULL) {
		call_interaction_id.s = (char *) json_object_get_string(item);
		call_interaction_id.len = strlen(call_interaction_id.s);
	}

	for (call_info_targets xtargets = targets; (target = *xtargets) != NULL; xtargets++)
	{

		if (cfg_get(kz_sca, kz_sca_cfg, process_no_subscribers) || kz_sca_subscription_aor_has_subscribers(KZ_SCA_EVENT_TYPE_CALL_INFO, target)) {

			SCA_DBG("\n\ntarget => %.*s\n\n", STR_FMT(target));

			kz_sca_appearance_t *app = NULL;
			int terminate_line_seize_sub = 0;
			int slot_idx = kz_sca_hash_table_index_for_key(sca->appearances, target);

			if (slot_idx < 0) {
				SCA_DBG("target no slot_idx => %.*s\n", STR_FMT(target));
				continue;
			}

			kz_sca_hash_table_lock_index(sca->appearances, slot_idx);

			app = kz_sca_appearance_for_dialog_unsafe_ex(sca, target, &dialog, slot_idx);

			if (app != NULL) {
				SCA_DBG("target => %.*s appearance exists\n", STR_FMT(target));

				if (call_info_state != KZ_SCA_APPEARANCE_STATE_IDLE) {
					if(kz_sca_appearance_update_unsafe(app, call_info_state, &to_display_name, &to_uri, &dialog, NULL, &to_uri) < 0) {
						LM_ERR("kz_sca_call_info_uri_update: failed to update appearance %.*s appearance-index %d with dialog id %.*s to state %s\n", STR_FMT(&app->owner), app->index, STR_FMT(&app->dialog.id), kz_sca_appearance_state_to_string(call_info_state));
					}
					kz_sca_appearance_update_switch_uri_unsafe(app, &switch_uri);
					kz_sca_appearance_update_ccvs_unsafe(app, &ccvs);
					kz_sca_appearance_update_aliases_unsafe(app, &aliases);
					kz_sca_appearance_update_direction_unsafe(app, direction);
					kz_sca_appearance_update_call_interaction_id_unsafe(app, &call_interaction_id);

				} else {
					if(!kz_sca_appearance_list_unlink_appearance(app->appearance_list, &app)) {
						LM_ERR("kz_sca_call_info_bye_handler: failed to unlink %.*s appearance-index %d, owner %.*s\n", STR_FMT(&app->owner), app->index, STR_FMT(&app->owner));
					}
					kz_sca_appearance_free(app);
				}
			} else {
				SCA_DBG("target => %.*s (%s) appearance does not exist\n", STR_FMT(target), kz_sca_appearance_state_to_string(call_info_state));

				// don't care if state is 'idle'
				if (call_info_state != KZ_SCA_APPEARANCE_STATE_IDLE) {
					if (direction == KZ_PRESENCE_DIRECTION_INITIATOR) {
						SCA_DBG("target => %.*s looking for seized index for %.*s\n", STR_FMT(target), STR_FMT(&endpoint_id));
						app = kz_sca_appearance_seized_index_unsafe(sca, target, &endpoint_id, slot_idx);
					}

					if (app == NULL) {
						if (cfg_get(kz_sca, kz_sca_cfg, use_next_available)) {
							app = kz_sca_appearance_seize_next_available_unsafe(sca, target, &endpoint_id, slot_idx);
						} else {
							app = kz_sca_appearance_seize_index_unsafe(sca, target, &endpoint_id, 0, slot_idx, NULL);
						}
					} else {
						SCA_DBG("target => %.*s promoting seized index\n", STR_FMT(target));
						terminate_line_seize_sub = 1;
					}

					if(app == NULL) {
						SCA_DBG("kz_sca_call_info_uri_update: failed to seize index %d for %.*s\n", 0, STR_FMT(target));
					} else {
						if(kz_sca_appearance_update_unsafe(app, call_info_state, &to_display_name, &to_uri, &dialog, target, &to_uri) < 0) {
							LM_ERR("kz_sca_call_info_uri_update: failed to update appearance %.*s appearance-index %d with dialog id %.*s to state %s\n", STR_FMT(&app->owner), app->index, STR_FMT(&app->dialog.id), kz_sca_appearance_state_to_string(call_info_state));
						}
						kz_sca_appearance_update_switch_uri_unsafe(app, &switch_uri);
						kz_sca_appearance_update_ccvs_unsafe(app, &ccvs);
						kz_sca_appearance_update_aliases_unsafe(app, &aliases);
						kz_sca_appearance_update_direction_unsafe(app, direction);
						kz_sca_appearance_update_call_interaction_id_unsafe(app, &call_interaction_id);
					}
				}

			}


			kz_sca_hash_table_unlock_index(sca->appearances, slot_idx);

			// need work here
			// enqueue request to update subscribers
			SCA_DBG("target notify => %.*s \n\n", STR_FMT(target));
			if(kz_sca_notify_call_info_subscribers(sca, target) < 0) {
				LM_ERR("kz_sca_call_info_ack_cb: failed to call-info NOTIFY %.*s subscribers\n", STR_FMT(target));
			}

			if (terminate_line_seize_sub) {
				SCA_DBG("target => %.*s promoted seized index, terminating line-seize subscription\n", STR_FMT(target));
				kz_sca_subscription_terminate(sca, target, KZ_SCA_EVENT_TYPE_LINE_SEIZE, &endpoint_id, KZ_SCA_SUBSCRIPTION_STATE_TERMINATED_NORESOURCE, KZ_SCA_SUBSCRIPTION_TERMINATE_OPT_UNSUBSCRIBE);
			}

			SCA_DBG("target finish => %.*s \n\n", STR_FMT(target));

		} else {
			SCA_DBG("no subscribers for => %.*s \n", STR_FMT(target));
		}

	}

	rc = 1;

end:

    if (json != NULL) {
    	json_object_put(json);
    }

	if (targets) {
		kz_sca_call_info_clean_targets(targets);
	}

	return rc;

}


/*
 * Map between $kz_call_info(idxname) and kz_sca_appearance_t
 *
 * index 1
 * state 2
 * uri 3
 * call_id 4
 * from_tag 5
 * to_tag 6
 * dialog 7
 * url 8
 * ccvs 9
 * owner 10
 * direction 11
 * aliases 12
 * interaction 13
 */

int kz_sca_pv_parse_call_info_name(pv_spec_p sp, str *in)
{
	if(sp == NULL || in == NULL || in->len <= 0)
		return -1;

	switch(in->len) {
		case 3:
			if(strncmp(in->s, "uri", 3) == 0) {
				sp->pvp.pvn.u.isname.name.n = 3;
			} else if(strncmp(in->s, "url", 3) == 0) {
				sp->pvp.pvn.u.isname.name.n = 8;
			} else {
				goto error;
			};
			break;

		case 4:
			if(strncmp(in->s, "ccvs", 4) == 0) {
				sp->pvp.pvn.u.isname.name.n = 9;
			} else {
				goto error;
			};
			break;

		case 5:
			if(strncmp(in->s, "index", 5) == 0) {
				sp->pvp.pvn.u.isname.name.n = 1;
			} else if(strncmp(in->s, "state", 5) == 0) {
				sp->pvp.pvn.u.isname.name.n = 2;
			} else if(strncmp(in->s, "owner", 5) == 0) {
				sp->pvp.pvn.u.isname.name.n = 10;
			} else {
				goto error;
			};
			break;

		case 6:
			if(strncmp(in->s, "to_tag", 6) == 0) {
				sp->pvp.pvn.u.isname.name.n = 6;
			} else if(strncmp(in->s, "dialog", 6) == 0) {
				sp->pvp.pvn.u.isname.name.n = 7;
			} else {
				goto error;
			};
			break;

		case 7:
			if(strncmp(in->s, "call_id", 7) == 0) {
				sp->pvp.pvn.u.isname.name.n = 4;
			} else if(strncmp(in->s, "aliases", 7) == 0) {
				sp->pvp.pvn.u.isname.name.n = 12;
			} else {
				goto error;
			};
			break;

		case 8:
			if(strncmp(in->s, "from_tag", 8) == 0) {
				sp->pvp.pvn.u.isname.name.n = 5;
			} else {
				goto error;
			};
			break;

		case 9:
			if(strncmp(in->s, "direction", 9) == 0) {
				sp->pvp.pvn.u.isname.name.n = 11;
			} else {
				goto error;
			};
			break;

		case 11:
			if(strncmp(in->s, "interaction", 11) == 0) {
				sp->pvp.pvn.u.isname.name.n = 13;
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

int kz_sca_pv_get_call_info(sip_msg_t *msg, pv_param_t *param, pv_value_t *res)
{

	kz_sca_appearance_t * app = NULL;
	kz_sca_call_info call_info;
	hdr_field_t *call_info_hdr;
	int slot_idx = -1;
	int ret = -1;
	str helper;
	char buffer[1024];

	if(parse_from_header(msg) < 0) {
		LM_ERR("Bad From header\n");
		return pv_get_null(msg, param, res);
	}

	memset(&call_info, 0, sizeof(kz_sca_call_info));
	call_info_hdr = get_hdr(msg, HDR_CALLINFO_T);

	if(KZ_SCA_HEADER_EMPTY(call_info_hdr)) {
		SCA_DBG("no call-info header\n");
		return pv_get_null(msg, param, res);
	}

	// this needs to accomodate comma-separated appearance info
	if(kz_sca_call_info_body_parse(&call_info_hdr->body, &call_info) < 0) {
		LM_ERR("Bad Call-Info header body: %.*s\n", STR_FMT(&call_info_hdr->body));
		return pv_get_null(msg, param, res);
	}

	slot_idx = kz_sca_hash_table_index_for_key(sca->appearances, &get_from(msg)->uri);
	if (slot_idx < 0) {
		SCA_DBG("slor idx for %.*s not found\n", STR_FMT(&get_from(msg)->uri));
		return pv_get_null(msg, param, res);
	}

	kz_sca_hash_table_lock_index(sca->appearances, slot_idx);

	app = kz_sca_appearance_for_index_unsafe(sca, &get_from(msg)->uri, call_info.index, slot_idx);
	if (app == NULL) {
		SCA_DBG("appearance not found %.*s\n", STR_FMT(&call_info_hdr->body));
		ret = pv_get_null(msg, param, res);
	} else if(param->pvn.u.isname.name.n == 1) {
		ret = pv_get_sintval(msg, param, res, app->index);
	} else if(param->pvn.u.isname.name.n == 2) {
		kz_sca_appearance_state_to_str(app->state, &helper);
		ret = pv_get_strval(msg, param, res, &helper);
	} else if(param->pvn.u.isname.name.n == 3) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->uri);
	} else if(param->pvn.u.isname.name.n == 4) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->dialog.call_id);
	} else if(param->pvn.u.isname.name.n == 5) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->dialog.from_tag);
	} else if(param->pvn.u.isname.name.n == 6) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->dialog.to_tag);
	} else if(param->pvn.u.isname.name.n == 7) {
		if (app->dialog.call_id.s) {
			sprintf(buffer, "%.*s%s%.*s%s%.*s", STR_FMT(&app->dialog.call_id),
				app->dialog.from_tag.len ? ";from-tag=" : "", STR_FMT(&app->dialog.from_tag),
				app->dialog.to_tag.len ? ";to-tag=" : "", STR_FMT(&app->dialog.to_tag));
			helper.s = buffer;
			helper.len = strlen(helper.s);
			ret = pv_get_strval(msg, param, res, &helper);
		} else {
			ret = pv_get_null(msg, param, res);
		}
	} else if(param->pvn.u.isname.name.n == 8) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->switch_uri);
	} else if(param->pvn.u.isname.name.n == 9) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->ccvs);
	} else if(param->pvn.u.isname.name.n == 10) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->owner);
	} else if(param->pvn.u.isname.name.n == 11) {
		kz_sca_presence_direction_t direction = app->direction;
		if (direction == KZ_PRESENCE_DIRECTION_NONE && app->state == KZ_SCA_APPEARANCE_STATE_SEIZED) {
			direction = KZ_PRESENCE_DIRECTION_INITIATOR;
		}
		kz_sca_presence_direction_to_str(direction, &helper);
		ret = pv_get_strval(msg, param, res, &helper);
	} else if(param->pvn.u.isname.name.n == 12) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->aliases);
	} else if(param->pvn.u.isname.name.n == 13) {
		ret = kz_pv_get_strval_null(msg, param, res, &app->call_interaction_id);
	}

	kz_sca_hash_table_unlock_index(sca->appearances, slot_idx);

	return ret;
}
