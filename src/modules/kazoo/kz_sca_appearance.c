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

#include "../../core/strutils.h"

#include "kz_sca.h"
#include "kz_sca_appearance.h"
#include "kz_sca_hash.h"
#include "kz_sca_notify.h"
#include "kz_sca_util.h"

const str KZ_SCA_APPEARANCE_INDEX_STR = STR_STATIC_INIT("appearance-index");
const str KZ_SCA_APPEARANCE_STATE_STR = STR_STATIC_INIT("appearance-state");
const str KZ_SCA_APPEARANCE_URI_STR = STR_STATIC_INIT("appearance-uri");

const str KZ_SCA_APPEARANCE_STATE_STR_IDLE = STR_STATIC_INIT("idle");
const str KZ_SCA_APPEARANCE_STATE_STR_SEIZED = STR_STATIC_INIT("seized");
const str KZ_SCA_APPEARANCE_STATE_STR_PROGRESSING = STR_STATIC_INIT("progressing");
const str KZ_SCA_APPEARANCE_STATE_STR_ALERTING = STR_STATIC_INIT("alerting");
const str KZ_SCA_APPEARANCE_STATE_STR_ACTIVE = STR_STATIC_INIT("active");
const str KZ_SCA_APPEARANCE_STATE_STR_ACTIVE_PENDING = STR_STATIC_INIT("active-pending");
const str KZ_SCA_APPEARANCE_STATE_STR_HELD = STR_STATIC_INIT("held");
const str KZ_SCA_APPEARANCE_STATE_STR_HELD_PRIVATE = STR_STATIC_INIT("held-private");
const str KZ_SCA_APPEARANCE_STATE_STR_UNKNOWN = STR_STATIC_INIT("unknown");

// STR_ACTIVE is repeated, once for ACTIVE_PENDING, once for ACTIVE
const str *state_names[] = {
		&KZ_SCA_APPEARANCE_STATE_STR_IDLE,
		&KZ_SCA_APPEARANCE_STATE_STR_SEIZED,
		&KZ_SCA_APPEARANCE_STATE_STR_PROGRESSING,
		&KZ_SCA_APPEARANCE_STATE_STR_ALERTING,
		&KZ_SCA_APPEARANCE_STATE_STR_ACTIVE_PENDING,
		&KZ_SCA_APPEARANCE_STATE_STR_ACTIVE,
		&KZ_SCA_APPEARANCE_STATE_STR_HELD,
		&KZ_SCA_APPEARANCE_STATE_STR_HELD_PRIVATE,
};

#define KZ_SCA_APPEARANCE_STATE_NAME_COUNT \
	(sizeof(state_names) / sizeof(state_names[0]))

const char * kz_sca_appearance_state_to_string(int state)
{
	if(state >= KZ_SCA_APPEARANCE_STATE_NAME_COUNT || state < 0) {
		return KZ_SCA_APPEARANCE_STATE_STR_UNKNOWN.s;
	}
	return state_names[state]->s;
}

void kz_sca_appearance_state_to_str(int state, str *state_str)
{
	assert(state_str != NULL);

	state_str->s = (char *) kz_sca_appearance_state_to_string(state);
	state_str->len = strlen(state_str->s);
}

int kz_sca_appearance_state_from_str(str *state_str)
{
	int state;

	assert(state_str != NULL);

	for(state = 0; state < KZ_SCA_APPEARANCE_STATE_NAME_COUNT; state++) {
		if(KZ_SCA_STR_EQ(state_str, state_names[state])) {
			break;
		}
	}
	if(state >= KZ_SCA_APPEARANCE_STATE_NAME_COUNT) {
		state = KZ_SCA_APPEARANCE_STATE_UNKNOWN;
	}

	return (state);
}

int kz_sca_appearance_state_from_string(const char * state_string)
{
	str state_str;
	state_str.s = (char *) state_string;
	state_str.len = strlen(state_str.s);
	return kz_sca_appearance_state_from_str(&state_str);
}

kz_sca_appearance_t *kz_sca_appearance_create(int appearance_index, str *owner_uri)
{
	kz_sca_appearance_t *new_appearance = NULL;

	// we use multiple shm_malloc calls here because uri, owner,
	// dialog and callee are mutable. could also shm_malloc a big
	// block and divide it among the strs....
	new_appearance = (kz_sca_appearance_t *)shm_malloc(sizeof(kz_sca_appearance_t));
	if(new_appearance == NULL) {
		LM_ERR("Failed to shm_malloc new kz_sca_appearance_t for %.*s, index %d\n", STR_FMT(owner_uri), appearance_index);
		goto error;
	}
	memset(new_appearance, 0, sizeof(kz_sca_appearance_t));

	new_appearance->owner.s = (char *)shm_malloc(owner_uri->len);
	if(new_appearance->owner.s == NULL) {
		LM_ERR("Failed to shm_malloc space for owner %.*s, index %d\n", STR_FMT(owner_uri), appearance_index);
		goto error;
	}
	KZ_SCA_STR_COPY(&new_appearance->owner, owner_uri);

	new_appearance->index = appearance_index;
	new_appearance->times.ctime = time(NULL);
	kz_sca_appearance_update_state_unsafe(new_appearance, KZ_SCA_APPEARANCE_STATE_IDLE);
	new_appearance->next = NULL;

	return (new_appearance);

error:
	if(new_appearance != NULL) {
		if(!KZ_SCA_STR_EMPTY(&new_appearance->owner)) {
			shm_free(new_appearance->owner.s);
		}
		shm_free(new_appearance);
	}
	return (NULL);
}

void kz_sca_appearance_free(kz_sca_appearance_t *appearance)
{
	if(appearance != NULL) {
		if(appearance->owner.s != NULL) {
			shm_free(appearance->owner.s);
		}
		if(appearance->uri.s != NULL) {
			shm_free(appearance->uri.s);
		}
		if(appearance->switch_uri.s != NULL) {
			shm_free(appearance->switch_uri.s);
		}
		if(appearance->dialog.id.s != NULL) {
			shm_free(appearance->dialog.id.s);
		}

		if(appearance->prev_owner.s != NULL) {
			shm_free(appearance->prev_owner.s);
		}
		if(appearance->prev_callee.s != NULL) {
			shm_free(appearance->prev_callee.s);
		}
		if(appearance->prev_dialog.id.s != NULL) {
			shm_free(appearance->prev_dialog.id.s);
		}
		shm_free(appearance);
	}
}

/*
 * assumes slot for app_entries is locked.
 *
 * appearance-index values are 1-indexed.
 * return values: 
 *	 -1:	error
 *	>=1:	index reserved for claimant
 */
static int kz_sca_appearance_list_next_available_index_unsafe(kz_sca_appearance_list *app_list)
{
	kz_sca_appearance_t *app_cur;
	int idx = 1;

	assert(app_list != NULL);

	for(app_cur = app_list->appearances; app_cur != NULL; app_cur = app_cur->next, idx++) {
		if(idx < app_cur->index) {
			break;
		}
	}

	return (idx);
}

static kz_sca_appearance_list *kz_sca_appearance_list_create(kz_sca_mod *scam, str *aor)
{
	kz_sca_appearance_list *app_list;
	int len;

	len = sizeof(kz_sca_appearance_list) + aor->len;
	app_list = (kz_sca_appearance_list *)shm_malloc(len);
	if(app_list == NULL) {
		LM_ERR("Failed to shm_malloc kz_sca_appearance_list for %.*s\n", STR_FMT(aor));
		return (NULL);
	}
	memset(app_list, 0, sizeof(kz_sca_appearance_list));
	len = sizeof(kz_sca_appearance_list);
	app_list->aor.s = (char *)app_list + len;
	KZ_SCA_STR_COPY(&app_list->aor, aor);

	return (app_list);
}

kz_sca_appearance_list *kz_sca_appearance_list_for_line(kz_sca_mod *scam, str *aor)
{
	//kz_sca_appearance_list
	return (NULL);
}

void kz_sca_appearance_list_insert_appearance(kz_sca_appearance_list *app_list, kz_sca_appearance_t *app)
{
	kz_sca_appearance_t **cur;

	assert(app_list != NULL);
	assert(app != NULL);

	app->appearance_list = app_list;

	for(cur = &app_list->appearances; *cur != NULL; cur = &(*cur)->next) {
		if(app->index < (*cur)->index) {
			break;
		}
	}
	app->next = *cur;
	*cur = app;
}

kz_sca_appearance_t *kz_sca_appearance_list_unlink_index(kz_sca_appearance_list *app_list, int idx)
{
	kz_sca_appearance_t *app = NULL;
	kz_sca_appearance_t **cur;

	assert(app_list != NULL);
	assert(idx > 0);

	for(cur = &app_list->appearances; *cur != NULL; cur = &(*cur)->next) {
		if((*cur)->index == idx) {
			app = *cur;
			app->appearance_list = NULL;
			*cur = (*cur)->next;
			break;
		}
	}

	if(app == NULL) {
		LM_ERR("Tried to remove inactive %.*s appearance at index %d\n", STR_FMT(&app_list->aor), idx);
	}
	return (app);
}

int kz_sca_appearance_list_unlink_appearance(kz_sca_appearance_list *app_list, kz_sca_appearance_t **app)
{
	kz_sca_appearance_t **cur;
	int rc = 0;

	assert(app_list != NULL);
	assert(app != NULL && *app != NULL);

	for(cur = &app_list->appearances; *cur != NULL; cur = &(*cur)->next) {
		if(*cur == *app) {
			*cur = (*cur)->next;
			(*app)->appearance_list = NULL;
			(*app)->next = NULL;
			rc = 1;
			break;
		}
	}

	return (rc);
}

int kz_sca_appearance_list_aor_cmp(str *aor, void *cmp_value)
{
	kz_sca_appearance_list *app_list = (kz_sca_appearance_list *)cmp_value;
	int cmp;

	if((cmp = aor->len - app_list->aor.len) != 0) {
		return (cmp);
	}

	return (memcmp(aor->s, app_list->aor.s, aor->len));
}

void kz_sca_appearance_list_print(void *value)
{
	kz_sca_appearance_list *app_list = (kz_sca_appearance_list *)value;
	kz_sca_appearance_t *app;
	str state_str = STR_NULL;

	LM_INFO("Appearance state for AoR %.*s:\n", STR_FMT(&app_list->aor));

	for(app = app_list->appearances; app != NULL; app = app->next) {
		kz_sca_appearance_state_to_str(app->state, &state_str);
		LM_INFO("index: %d, state: %.*s, uri: %.*s, owner: %.*s, "
				"callee: %.*s, dialog: %.*s;%.*s;%.*s\n",
				app->index, STR_FMT(&state_str), STR_FMT(&app->uri),
				STR_FMT(&app->owner), STR_FMT(&app->callee),
				STR_FMT(&app->dialog.call_id), STR_FMT(&app->dialog.from_tag),
				STR_FMT(&app->dialog.to_tag));
	}
}

void kz_sca_appearance_list_free(void *value)
{
	kz_sca_appearance_list *app_list = (kz_sca_appearance_list *)value;
	kz_sca_appearance_t *app, *app_tmp;

	SCA_DBG("Freeing appearance list for AoR %.*s\n", STR_FMT(&app_list->aor));

	for(app = app_list->appearances; app != NULL; app = app_tmp) {
		app_tmp = app->next;
		kz_sca_appearance_free(app);
	}
	shm_free(app_list);
}

int kz_sca_appearance_register(kz_sca_mod *scam, str *aor)
{
	kz_sca_appearance_list *app_list;
	int rc = -1;

	assert(scam != NULL);
	assert(aor != NULL);

	if(kz_sca_uri_is_shared_appearance(scam, aor)) {
		// we've already registered
		rc = 0;
		goto done;
	}

	app_list = kz_sca_appearance_list_create(scam, aor);
	if(app_list == NULL) {
		goto done;
	}

	if(kz_sca_hash_table_kv_insert(scam->appearances, aor, app_list, kz_sca_appearance_list_aor_cmp, kz_sca_appearance_list_print, kz_sca_appearance_list_free) < 0) {
		LM_ERR("kz_sca_appearance_register: failed to insert appearance list for %.*s\n", STR_FMT(aor));
		goto done;
	}

	rc = 1;

done:
	return (rc);
}

int kz_sca_appearance_unregister(kz_sca_mod *scam, str *aor)
{
	int rc = 0;

	assert(scam != NULL);
	assert(aor != NULL);

	if(kz_sca_uri_is_shared_appearance(scam, aor)) {
		if((rc = kz_sca_hash_table_kv_delete(scam->appearances, aor)) == 0) {
			rc = 1;
			LM_INFO("unregistered KZ_SCA AoR %.*s\n", STR_FMT(aor));
		}
	}

	return (rc);
}

kz_sca_appearance_t *kz_sca_appearance_seize_index_unsafe(kz_sca_mod *scam, str *aor, str *owner_uri, int app_idx, int slot_idx, int *seize_error)
{
	kz_sca_appearance_list *app_list;
	kz_sca_appearance_t *app = NULL;
	kz_sca_hash_slot *slot;
	int error = KZ_SCA_APPEARANCE_ERR_UNKNOWN;

	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, aor);
	if(app_list == NULL) {
		SCA_DBG("kz_sca_appearance_seize_index_unsafe: no appearance list for %.*s\n", STR_FMT(aor));
		goto done;
	}

	if(app_idx <= 0) {
		app_idx = kz_sca_appearance_list_next_available_index_unsafe(app_list);
	}

	for(app = app_list->appearances; app != NULL; app = app->next) {
		if(app->index >= app_idx) {
			break;
		}
	}

	if(app != NULL && app->index == app_idx) {
		// attempt to seize in-use appearance-index
		error = KZ_SCA_APPEARANCE_ERR_INDEX_UNAVAILABLE;
		app = NULL;
		goto done;
	}

	app = kz_sca_appearance_create(app_idx, owner_uri);

	if(app == NULL) {
		LM_ERR("Failed to create new appearance for %.*s at index %d\n", STR_FMT(owner_uri), app_idx);
		error = KZ_SCA_APPEARANCE_ERR_MALLOC;
		goto done;
	}

	kz_sca_appearance_update_state_unsafe(app, KZ_SCA_APPEARANCE_STATE_SEIZED);
	kz_sca_appearance_list_insert_appearance(app_list, app);
	error = KZ_SCA_APPEARANCE_OK;

done:
	if(seize_error) {
		*seize_error = error;
	}

	return (app);
}

int kz_sca_appearance_seize_index(kz_sca_mod *scam, str *aor, int idx, str *owner_uri)
{
	kz_sca_appearance_t *app;
	int slot_idx;
	int app_idx = -1;
	int error = KZ_SCA_APPEARANCE_OK;

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);

	app = kz_sca_appearance_seize_index_unsafe(scam, aor, owner_uri, idx, slot_idx, &error);
	if(app != NULL) {
		app_idx = app->index;
	}

	kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);

	if(error == KZ_SCA_APPEARANCE_ERR_INDEX_UNAVAILABLE) {
		app_idx = KZ_SCA_APPEARANCE_INDEX_UNAVAILABLE;
	}

	return (app_idx);
}

kz_sca_appearance_t *kz_sca_appearance_seize_next_available_unsafe(kz_sca_mod *scam, str *aor, str *owner_uri, int slot_idx)
{
	kz_sca_appearance_list *app_list;
	kz_sca_appearance_t *app = NULL;
	kz_sca_hash_slot *slot;
	int idx = -1;

	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, aor);
	if(app_list == NULL) {
		app_list = kz_sca_appearance_list_create(scam, aor);
		if(app_list == NULL) {
			goto done;
		}

		if(kz_sca_hash_table_slot_kv_insert_unsafe(slot, app_list, kz_sca_appearance_list_aor_cmp, kz_sca_appearance_list_print, kz_sca_appearance_list_free) < 0) {
			LM_ERR("Failed to insert appearance list for %.*s\n", STR_FMT(aor));
			goto done;
		}
	}

	// XXX this grows without bound. add modparam to set a hard limit
	idx = kz_sca_appearance_list_next_available_index_unsafe(app_list);
	// XXX check idx > any configured max appearance index

	app = kz_sca_appearance_create(idx, owner_uri);
	if(app == NULL) {
		LM_ERR("Failed to create new appearance for %.*s at index %d\n", STR_FMT(owner_uri), idx);
		goto done;
	}
	kz_sca_appearance_update_state_unsafe(app, KZ_SCA_APPEARANCE_STATE_SEIZED);

	kz_sca_appearance_list_insert_appearance(app_list, app);

done:
	return (app);
}

int kz_sca_appearance_seize_next_available_index(kz_sca_mod *scam, str *aor, str *owner_uri)
{
	kz_sca_appearance_t *app;
	int slot_idx;
	int idx = -1;

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);

	app = kz_sca_appearance_seize_next_available_unsafe(scam, aor, owner_uri, slot_idx);
	if(app != NULL) {
		idx = app->index;
	}

	kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);

	return (idx);
}

void kz_sca_appearance_update_state_unsafe(kz_sca_appearance_t *app, int state)
{
	assert(app != NULL);

	app->state = state;
	app->times.mtime = time(NULL);
}

int kz_sca_appearance_update_owner_unsafe(kz_sca_appearance_t *app, str *owner)
{
	assert(app != NULL);
	assert(owner != NULL);

	if(!KZ_SCA_STR_EMPTY(&app->owner)) {
		if(app->prev_owner.s != NULL) {
			shm_free(app->prev_owner.s);
		}
		app->prev_owner.s = app->owner.s;
		app->prev_owner.len = app->owner.len;
	}

	app->owner.s = (char *)shm_malloc(owner->len);
	if(app->owner.s == NULL) {
		LM_ERR("kz_sca_appearance_update_owner_unsafe: shm_malloc for new owner %.*s failed: out of memory\n", STR_FMT(owner));
		goto error;
	}
	KZ_SCA_STR_COPY(&app->owner, owner);

	return (1);

error:
	// restore owner
	app->owner.s = app->prev_owner.s;
	app->owner.len = app->prev_owner.len;
	memset(&app->prev_owner, 0, sizeof(str));

	return (-1);
}

int kz_sca_appearance_update_switch_uri_unsafe(kz_sca_appearance_t *app, str *switch_uri)
{
	assert(app != NULL);
	assert(switch_uri != NULL);
	char * buffer = NULL;

	if(KZ_SCA_STR_EQ(&app->switch_uri, switch_uri)) {
		return 1;
	}

	buffer = (char *)shm_malloc(switch_uri->len);

	if(buffer == NULL) {
		LM_ERR("kz_sca_appearance_update_owner_unsafe: shm_malloc for new switch_uri %.*s failed: out of memory\n", STR_FMT(switch_uri));
		return -1;
	}

	if (app->switch_uri.s) {
		shm_free(app->switch_uri.s);
	}

	app->switch_uri.s = buffer;

	KZ_SCA_STR_COPY(&app->switch_uri, switch_uri);

	return 1;
}

int kz_sca_appearance_update_ccvs_unsafe(kz_sca_appearance_t *app, str *ccvs)
{
	assert(app != NULL);
	assert(ccvs != NULL);
	char * buffer = NULL;

	if(KZ_SCA_STR_EQ(&app->ccvs, ccvs)) {
		return 1;
	}

	buffer = (char *)shm_malloc(ccvs->len);

	if(buffer == NULL) {
		LM_ERR("kz_sca_appearance_update_owner_unsafe: shm_malloc for new ccvs %.*s failed: out of memory\n", STR_FMT(ccvs));
		return -1;
	}

	if (app->ccvs.s) {
		shm_free(app->ccvs.s);
	}

	app->ccvs.s = buffer;

	KZ_SCA_STR_COPY(&app->ccvs, ccvs);

	return 1;
}

int kz_sca_appearance_update_aliases_unsafe(kz_sca_appearance_t *app, str *aliases)
{
	assert(app != NULL);
	assert(aliases != NULL);
	char * buffer = NULL;

	if(KZ_SCA_STR_EQ(&app->aliases, aliases)) {
		return 1;
	}

	buffer = (char *)shm_malloc(aliases->len);

	if(buffer == NULL) {
		LM_ERR("kz_sca_appearance_update_owner_unsafe: shm_malloc for new aliases %.*s failed: out of memory\n", STR_FMT(aliases));
		return -1;
	}

	if (app->aliases.s) {
		shm_free(app->aliases.s);
	}

	app->aliases.s = buffer;

	KZ_SCA_STR_COPY(&app->aliases, aliases);

	return 1;
}

int kz_sca_appearance_update_direction_unsafe(kz_sca_appearance_t *app, kz_sca_presence_direction_t direction)
{
	assert(app != NULL);
	app->direction = direction;
	return 1;
}

int kz_sca_appearance_update_call_interaction_id_unsafe(kz_sca_appearance_t *app, str *call_interaction_id)
{
	assert(app != NULL);
	assert(call_interaction_id != NULL);
	char * buffer = NULL;

	if(KZ_SCA_STR_EQ(&app->call_interaction_id, call_interaction_id)) {
		return 1;
	}

	buffer = (char *)shm_malloc(call_interaction_id->len);

	if(buffer == NULL) {
		LM_ERR("kz_sca_appearance_update_owner_unsafe: shm_malloc for new call_interaction_id %.*s failed: out of memory\n", STR_FMT(call_interaction_id));
		return -1;
	}

	if (app->call_interaction_id.s) {
		shm_free(app->call_interaction_id.s);
	}

	app->call_interaction_id.s = buffer;

	KZ_SCA_STR_COPY(&app->call_interaction_id, call_interaction_id);

	return 1;

}

int kz_sca_appearance_update_callee_unsafe(kz_sca_appearance_t *app, str *callee)
{
	assert(app != NULL);
	assert(callee != NULL);

	if(!KZ_SCA_STR_EMPTY(&app->callee)) {
		if(app->prev_callee.s != NULL) {
			shm_free(app->prev_callee.s);
		}
		app->prev_callee.s = app->callee.s;
		app->prev_callee.len = app->callee.len;
	}

	app->callee.s = (char *)shm_malloc(callee->len);
	if(app->callee.s == NULL) {
		LM_ERR("kz_sca_appearance_update_owner_unsafe: shm_malloc for new callee %.*s failed: out of memory\n", STR_FMT(callee));
		goto error;
	}
	KZ_SCA_STR_COPY(&app->callee, callee);

	return (1);

error:
	// restore callee
	app->callee.s = app->prev_callee.s;
	app->callee.len = app->prev_callee.len;
	memset(&app->prev_callee, 0, sizeof(str));

	return (-1);
}

int kz_sca_appearance_update_dialog_unsafe(kz_sca_appearance_t *app, str *call_id, str *from_tag, str *to_tag)
{
	int len;

	assert(app != NULL);
	assert(call_id != NULL);
	assert(from_tag != NULL);

	if(!KZ_SCA_STR_EMPTY(&app->dialog.id)) {
		if(app->prev_dialog.id.s != NULL) {
			shm_free(app->prev_dialog.id.s);
		}
		app->prev_dialog.id.s = app->dialog.id.s;
		app->prev_dialog.id.len = app->dialog.id.len;

		app->prev_dialog.call_id.s = app->dialog.call_id.s;
		app->prev_dialog.call_id.len = app->dialog.call_id.len;

		app->prev_dialog.from_tag.s = app->dialog.from_tag.s;
		app->prev_dialog.from_tag.len = app->dialog.from_tag.len;

		app->prev_dialog.to_tag.s = app->dialog.to_tag.s;
		app->prev_dialog.to_tag.len = app->dialog.to_tag.len;
	}

	len = call_id->len + from_tag->len;
	if(!KZ_SCA_STR_EMPTY(to_tag)) {
		len += to_tag->len;
	}

	app->dialog.id.s = (char *)shm_malloc(len);
	if(app->dialog.id.s == NULL) {
		LM_ERR("kz_sca_appearance_update_dialog_unsafe: shm_malloc new dialog failed: out of memory\n");
		goto error;
	}
	KZ_SCA_STR_COPY(&app->dialog.id, call_id);
	KZ_SCA_STR_APPEND(&app->dialog.id, from_tag);

	app->dialog.call_id.s = app->dialog.id.s;
	app->dialog.call_id.len = call_id->len;

	app->dialog.from_tag.s = app->dialog.id.s + call_id->len;
	app->dialog.from_tag.len = from_tag->len;

	app->dialog.to_tag.s = app->dialog.id.s + call_id->len + from_tag->len;
	app->dialog.to_tag.len = to_tag->len;

	return (1);

error:
	// restore dialog
	app->prev_dialog.id.s = app->dialog.id.s;
	app->prev_dialog.id.len = app->dialog.id.len;

	app->prev_dialog.call_id.s = app->dialog.call_id.s;
	app->prev_dialog.call_id.len = app->dialog.call_id.len;

	app->prev_dialog.from_tag.s = app->dialog.from_tag.s;
	app->prev_dialog.from_tag.len = app->dialog.from_tag.len;

	app->prev_dialog.to_tag.s = app->dialog.to_tag.s;
	app->prev_dialog.to_tag.len = app->dialog.to_tag.len;

	memset(&app->prev_dialog, 0, sizeof(kz_sca_dialog));

	return (-1);
}

int kz_sca_appearance_update_unsafe(kz_sca_appearance_t *app, int state, str *display, str *uri, kz_sca_dialog *dialog, str *owner, str *callee)
{
	int rc = KZ_SCA_APPEARANCE_OK;
	int len;

	if(state != KZ_SCA_APPEARANCE_STATE_UNKNOWN) {
		kz_sca_appearance_update_state_unsafe(app, state);
	}

	if(!KZ_SCA_STR_EMPTY(uri)) {
		if(!KZ_SCA_STR_EMPTY(&app->uri)) {
			// the uri str's s member is shm_malloc'd separately
			shm_free(app->uri.s);
			memset(&app->uri, 0, sizeof(str));
		}

		// +2 for left & right carets surrounding URI
		len = uri->len + 2;
		if(!KZ_SCA_STR_EMPTY(display)) {
			// cheaper to scan string than shm_malloc 2x display?
			len += kz_sca_uri_display_escapes_count(display);
			// +1 for space between display & uri
			len += display->len + 1;
		}
		app->uri.s = (char *)shm_malloc(len);
		if(app->uri.s == NULL) {
			LM_ERR("shm_malloc %d bytes returned NULL\n", uri->len);
			rc = KZ_SCA_APPEARANCE_ERR_MALLOC;
			goto done;
		}

		if(!KZ_SCA_STR_EMPTY(display)) {
			// copy escaped display information...
			app->uri.len = escape_common(app->uri.s, display->s, display->len);

			// ... and add a space between it and the uri
			*(app->uri.s + app->uri.len) = ' ';
			app->uri.len++;
		}

		*(app->uri.s + app->uri.len) = '<';
		app->uri.len++;

		KZ_SCA_STR_APPEND(&app->uri, uri);

		*(app->uri.s + app->uri.len) = '>';
		app->uri.len++;
	}

	if(!KZ_SCA_DIALOG_EMPTY(dialog)) {
		if(!KZ_SCA_STR_EQ(&dialog->id, &app->dialog.id) || !KZ_SCA_STR_EQ(&dialog->from_tag, &app->dialog.from_tag) || !KZ_SCA_STR_EQ(&dialog->to_tag, &app->dialog.to_tag)) {

			int len = dialog->call_id.len + dialog->from_tag.len;
			if(!KZ_SCA_STR_EMPTY(&dialog->to_tag)) {
				len += dialog->to_tag.len;
			}

			if(app->dialog.id.s != NULL) {
				shm_free(app->dialog.id.s);
			}

			app->dialog.id.s = (char *)shm_malloc(len);
			app->dialog.id.len = 0;
			if(app->dialog.id.s == NULL) {
				LM_ERR("kz_sca_appearance_update_unsafe: shm_malloc dialog id failed: out of shared memory\n");
				// XXX this seems bad enough to abort...
				return (-1);
			}

			memcpy(app->dialog.id.s, dialog->id.s, len);
			app->dialog.id.len = dialog->id.len;
			//KZ_SCA_STR_COPY(&app->dialog.id, &dialog->id);

			app->dialog.call_id.s = app->dialog.id.s;
			app->dialog.call_id.len = dialog->call_id.len;

			app->dialog.from_tag.s = app->dialog.id.s + dialog->call_id.len;
			app->dialog.from_tag.len = dialog->from_tag.len;

			if(!KZ_SCA_STR_EMPTY(&dialog->to_tag)) {
				app->dialog.to_tag.s = app->dialog.id.s + dialog->call_id.len + dialog->from_tag.len;
				app->dialog.to_tag.len = dialog->to_tag.len;
			} else {
				app->dialog.to_tag.s = NULL;
				app->dialog.to_tag.len = 0;
			}
		}
	}

	// NOTE these two blocks could be condensed and inlined
	if(!KZ_SCA_STR_EMPTY(owner)) {
		if(!KZ_SCA_STR_EQ(&app->owner, owner)) {
			if(app->owner.s != NULL) {
				shm_free(app->owner.s);
			}

			app->owner.s = (char *)shm_malloc(owner->len);
			if(app->owner.s == NULL) {
				LM_ERR("kz_sca_appearance_update_unsafe: shm_malloc appearance owner URI failed: out of shared memory\n");
				return (-1);
			}
			KZ_SCA_STR_COPY(&app->owner, owner);
		}
	}

	if(!KZ_SCA_STR_EMPTY(callee)) {
		if(!KZ_SCA_STR_EQ(&app->callee, callee)) {
			if(app->callee.s != NULL) {
				shm_free(app->callee.s);
			}

			app->callee.s = (char *)shm_malloc(callee->len);
			if(app->callee.s == NULL) {
				LM_ERR("kz_sca_appearance_update_unsafe: shm_malloc appearance callee URI failed: out of shared memory\n");
				return (-1);
			}
			KZ_SCA_STR_COPY(&app->callee, callee);
		}
	}

done:
	return (rc);
}

int kz_sca_uri_is_shared_appearance(kz_sca_mod *scam, str *aor)
{
	kz_sca_hash_slot *slot;
	kz_sca_appearance_list *app_list;
	int slot_idx;

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);
	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, aor);
	kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);

	if(app_list == NULL) {
		return (0);
	}

	return (1);
}

int kz_sca_uri_lock_shared_appearance(kz_sca_mod *scam, str *aor)
{
	kz_sca_hash_slot *slot;
	kz_sca_appearance_list *app_list;
	int slot_idx;

	if(KZ_SCA_STR_EMPTY(aor)) {
		return (-1);
	}

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);
	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, aor);

	if(app_list == NULL) {
		kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);
		slot_idx = -1;
	}

	return (slot_idx);
}

int kz_sca_uri_lock_if_shared_appearance(kz_sca_mod *scam, str *aor, int *slot_idx)
{
	kz_sca_hash_slot *slot;
	kz_sca_appearance_list *app_list;

	assert(slot_idx != NULL);

	if(KZ_SCA_STR_EMPTY(aor)) {
		*slot_idx = -1;
		return (0);
	}

	*slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	slot = kz_sca_hash_table_slot_for_index(scam->appearances, *slot_idx);

	kz_sca_hash_table_lock_index(scam->appearances, *slot_idx);
	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, aor);

	if(app_list == NULL) {
		kz_sca_hash_table_unlock_index(scam->appearances, *slot_idx);
		*slot_idx = -1;

		return (0);
	}

	return (1);
}

int kz_sca_appearance_state_for_index(kz_sca_mod *scam, str *aor, int idx)
{
	kz_sca_hash_slot *slot;
	kz_sca_appearance_list *app_list;
	kz_sca_appearance_t *app;
	int slot_idx;
	int state = KZ_SCA_APPEARANCE_STATE_UNKNOWN;

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);

	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, aor);
	if(app_list == NULL) {
		SCA_DBG("%.*s has no in-use appearances\n", STR_FMT(aor));
		goto done;
	}

	for(app = app_list->appearances; app != NULL; app = app->next) {
		if(app->index == idx) {
			break;
		}
	}
	if(app == NULL) {
		LM_WARN("%.*s appearance-index %d is not in use\n", STR_FMT(aor), idx);
		goto done;
	}

	state = app->state;

done:
	kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);

	return (state);
}

int kz_sca_appearance_update_index(kz_sca_mod *scam, str *aor, int idx, int state, str *display, str *uri, kz_sca_dialog *dialog)
{
	kz_sca_hash_slot *slot;
	kz_sca_appearance_list *app_list;
	kz_sca_appearance_t *app;
	str state_str = STR_NULL;
	int len;
	int slot_idx;
	int rc = KZ_SCA_APPEARANCE_ERR_UNKNOWN;

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);

	kz_sca_appearance_state_to_str(state, &state_str);

	app_list = kz_sca_hash_table_slot_kv_find_unsafe(slot, aor);
	if(app_list == NULL) {
		LM_WARN("Cannot update %.*s index %d to state %.*s: %.*s has no in-use appearances\n", STR_FMT(aor), idx, STR_FMT(&state_str), STR_FMT(aor));
		rc = KZ_SCA_APPEARANCE_ERR_NOT_IN_USE;
		goto done;
	}
	SCA_DBG("search for index[%d] dialog.id[%.*s]\n", idx, STR_FMT(&dialog->id));
	for(app = app_list->appearances; app != NULL; app = app->next) {
		SCA_DBG("app.index[%d] app.dialog.id[%.*s]\n", app->index, STR_FMT(&app->dialog.id));
		if(app->index == idx) {
			break;
		} else if(idx == 0) {
			if(KZ_SCA_STR_EQ(&dialog->id, &app->dialog.id)) {
				break;
			}
		}
	}
	if(app == NULL) {
		LM_WARN("Cannot update %.*s index %d to %.*s: index %d not in use\n", STR_FMT(aor), idx, STR_FMT(&state_str), idx);
		rc = KZ_SCA_APPEARANCE_ERR_INDEX_INVALID;
		goto done;
	}

	if(state != KZ_SCA_APPEARANCE_STATE_UNKNOWN && app->state != state) {
		kz_sca_appearance_update_state_unsafe(app, state);
	}

	if(!KZ_SCA_STR_EMPTY(uri)) {
		if(!KZ_SCA_STR_EMPTY(&app->uri)) {
			// the uri str's s member is shm_malloc'd separately
			shm_free(app->uri.s);
			memset(&app->uri, 0, sizeof(str));
		}

		// +2 for left & right carets surrounding URI
		len = uri->len + 2;
		if(!KZ_SCA_STR_EMPTY(display)) {
			// cheaper to scan string than shm_malloc 2x display?
			len += kz_sca_uri_display_escapes_count(display);
			// +1 for space between display & uri
			len += display->len + 1;
		}
		app->uri.s = (char *)shm_malloc(len);
		if(app->uri.s == NULL) {
			LM_ERR("Failed to update %.*s index %d uri to %.*s: shm_malloc %d bytes returned NULL\n", STR_FMT(aor), idx, STR_FMT(uri), uri->len);
			rc = KZ_SCA_APPEARANCE_ERR_MALLOC;
			goto done;
		}

		if(!KZ_SCA_STR_EMPTY(display)) {
			// copy escaped display information...
			app->uri.len = escape_common(app->uri.s, display->s, display->len);

			// ... and add a space between it and the uri
			*(app->uri.s + app->uri.len) = ' ';
			app->uri.len++;
		}

		*(app->uri.s + app->uri.len) = '<';
		app->uri.len++;

		KZ_SCA_STR_APPEND(&app->uri, uri);

		*(app->uri.s + app->uri.len) = '>';
		app->uri.len++;
	}

	if(!KZ_SCA_DIALOG_EMPTY(dialog)) {
		if(!KZ_SCA_STR_EQ(&dialog->id, &app->dialog.id)) {
			if(app->dialog.id.s != NULL) {
				shm_free(app->dialog.id.s);
			}

			app->dialog.id.s = (char *)shm_malloc(dialog->id.len);
			KZ_SCA_STR_COPY(&app->dialog.id, &dialog->id);

			app->dialog.call_id.s = app->dialog.id.s;
			app->dialog.call_id.len = dialog->call_id.len;

			app->dialog.from_tag.s = app->dialog.id.s + dialog->call_id.len;
			app->dialog.from_tag.len = dialog->from_tag.len;

			if(!KZ_SCA_STR_EMPTY(&dialog->to_tag)) {
				app->dialog.to_tag.s = app->dialog.id.s + dialog->call_id.len + dialog->from_tag.len;
				app->dialog.to_tag.len = dialog->to_tag.len;
			} else {
				app->dialog.to_tag.s = NULL;
				app->dialog.to_tag.len = 0;
			}
		}
	}

	rc = KZ_SCA_APPEARANCE_OK;

done:
	kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);

	return (rc);
}

int kz_sca_appearance_release_index(kz_sca_mod *scam, str *aor, int idx)
{
	kz_sca_hash_slot *slot;
	kz_sca_hash_entry *ent;
	kz_sca_appearance_list *app_list = NULL;
	kz_sca_appearance_t *app;
	int slot_idx;
	int rc = KZ_SCA_APPEARANCE_ERR_UNKNOWN;

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	slot = kz_sca_hash_table_slot_for_index(scam->appearances, slot_idx);

	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);

	app_list = NULL;
	for(ent = slot->entries; ent != NULL; ent = ent->next) {
		if(ent->compare(aor, ent->value) == 0) {
			app_list = (kz_sca_appearance_list *)ent->value;
			break;
		}
	}
	if(app_list == NULL) {
		LM_ERR("No appearances for %.*s\n", STR_FMT(aor));
		rc = KZ_SCA_APPEARANCE_ERR_NOT_IN_USE;
		goto done;
	}

	app = kz_sca_appearance_list_unlink_index(app_list, idx);
	if(app == NULL) {
		LM_ERR("Failed to unlink %.*s appearance-index %d: invalid index\n", STR_FMT(aor), idx);
		rc = KZ_SCA_APPEARANCE_ERR_INDEX_INVALID;
		goto done;
	}
	kz_sca_appearance_free(app);

	rc = KZ_SCA_APPEARANCE_OK;

done:
	kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);

	return (rc);
}

int kz_sca_appearance_owner_release_all(str *aor, str *owner)
{
	kz_sca_appearance_list *app_list = NULL;
	kz_sca_appearance_t *app, **cur_app, **tmp_app;
	kz_sca_hash_slot *slot;
	kz_sca_hash_entry *ent;
	int slot_idx = -1;
	int released = -1;

	slot_idx = kz_sca_uri_lock_shared_appearance(sca, aor);
	if(slot_idx < 0) {
		SCA_DBG("kz_sca_appearance_owner_release_all: %.*s is not a shared appearance\n", STR_FMT(aor));
		return (0);
	}
	slot = kz_sca_hash_table_slot_for_index(sca->appearances, slot_idx);

	for(ent = slot->entries; ent != NULL; ent = ent->next) {
		if(ent->compare(aor, ent->value) == 0) {
			app_list = (kz_sca_appearance_list *)ent->value;
			break;
		}
	}

	released = 0;

	if(app_list == NULL) {
		SCA_DBG("kz_sca_appearance_owner_release_all: No appearances for %.*s\n", STR_FMT(aor));
		goto done;
	}

	for(cur_app = &app_list->appearances; *cur_app != NULL; cur_app = tmp_app) {
		tmp_app = &(*cur_app)->next;

		if(!KZ_SCA_STR_EQ(owner, &(*cur_app)->owner)) {
			continue;
		}

		app = *cur_app;
		*cur_app = (*cur_app)->next;
		tmp_app = cur_app;

		if(app) {
			kz_sca_appearance_free(app);
			released++;
		}
	}

done:
	if(slot_idx >= 0) {
		kz_sca_hash_table_unlock_index(sca->appearances, slot_idx);
	}

	return (released);
}

kz_sca_appearance_t *kz_sca_appearance_for_index_unsafe(kz_sca_mod *scam, str *aor, int app_idx, int slot_idx)
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
		LM_ERR("No appearances for %.*s\n", STR_FMT(aor));
		return (NULL);
	}

	for(app = app_list->appearances; app != NULL; app = app->next) {
		if(app->index == app_idx) {
			break;
		}
	}

	return (app);
}

kz_sca_appearance_t * kz_sca_appearance_for_dialog_unsafe(kz_sca_mod *scam, str *aor, kz_sca_dialog *dialog, int slot_idx)
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
		LM_ERR("No appearances for %.*s\n", STR_FMT(aor));
		return (NULL);
	}

	SCA_DBG("search for call_id[%.*s] from_tag[%.*s]\n", STR_FMT(&dialog->call_id), STR_FMT(&dialog->from_tag));
	for(app = app_list->appearances; app != NULL; app = app->next) {
		SCA_DBG("app.call_id[%.*s] app.from_tag[%.*s] app.to_tag[%.*s]\n", STR_FMT(&app->dialog.call_id), STR_FMT(&app->dialog.from_tag), STR_FMT(&app->dialog.to_tag));
		if(KZ_SCA_STR_EQ(&app->dialog.call_id, &dialog->call_id) && KZ_SCA_STR_EQ(&app->dialog.from_tag, &dialog->from_tag)) {
#ifdef notdef
			if(!KZ_SCA_STR_EMPTY(&app->dialog.to_tag)
					&& !KZ_SCA_STR_EMPTY(&dialog->to_tag)
					&& !KZ_SCA_STR_EQ(&app->dialog.to_tag, &dialog->to_tag)) {
				continue;
			}
#endif // notdef
			SCA_DBG("%.*s appearance-index %d found\n", STR_FMT(aor), app->index);
			break;
		}
	}

	return (app);
}

kz_sca_appearance_t *kz_sca_appearance_for_tags_unsafe(kz_sca_mod *scam, str *aor, str *call_id, str *from_tag, str *to_tag, int slot_idx)
{
	kz_sca_dialog dialog;
	char dlg_buf[1024];

	dialog.id.s = dlg_buf;
	if(kz_sca_dialog_build_from_tags(&dialog, sizeof(dlg_buf), call_id, from_tag, to_tag) < 0) {
		LM_ERR("kz_sca_appearance_for_tags_unsafe: failed to build dialog from tags\n");
		return (NULL);
	}

	return (kz_sca_appearance_for_dialog_unsafe(scam, aor, &dialog, slot_idx));
}

kz_sca_appearance_t *kz_sca_appearance_unlink_by_tags(kz_sca_mod *scam, str *aor, str *call_id, str *from_tag, str *to_tag)
{
	kz_sca_appearance_t *app = NULL, *unl_app;
	int slot_idx = -1;

	slot_idx = kz_sca_hash_table_index_for_key(scam->appearances, aor);
	kz_sca_hash_table_lock_index(scam->appearances, slot_idx);

	app = kz_sca_appearance_for_tags_unsafe(scam, aor, call_id, from_tag, to_tag, slot_idx);
	if(app == NULL) {
		LM_ERR("kz_sca_appearance_unlink_by_tags: no appearances found for %.*s with dialog %.*s;%.*s;%.*s\n", STR_FMT(aor), STR_FMT(call_id), STR_FMT(from_tag), STR_FMT(to_tag));
		goto done;
	}

	unl_app = kz_sca_appearance_list_unlink_index(app->appearance_list, app->index);
	if(unl_app == NULL || unl_app != app) {
		LM_ERR("kz_sca_appearance_unlink_by_tags: failed to unlink %.*s appearance-index %d\n", STR_FMT(aor), app->index);
		app = NULL;
		goto done;
	}

done:
	if(slot_idx >= 0) {
		kz_sca_hash_table_unlock_index(scam->appearances, slot_idx);
	}

	return (app);
}

void kz_sca_appearance_purge_stale(unsigned int ticks, void *param)
{
	struct notify_list
	{
		struct notify_list *next;
		str aor;
	};

	kz_sca_mod *scam = (kz_sca_mod *)param;
	kz_sca_hash_table *ht;
	kz_sca_hash_entry *ent;
	kz_sca_appearance_list *app_list;
	kz_sca_appearance_t **cur_app, **tmp_app, *app = NULL;
	struct notify_list *notify_list = NULL, *tmp_nl;
	int i;
	int unlinked;
	time_t now, ttl;

	SCA_DBG("KZ_SCA: purging stale appearances\n");

	assert(scam != NULL);
	assert(scam->appearances != NULL);

	now = time(NULL);

	ht = scam->appearances;
	for(i = 0; i < ht->size; i++) {
		kz_sca_hash_table_lock_index(ht, i);

		for(ent = ht->slots[i].entries; ent != NULL; ent = ent->next) {
			app_list = (kz_sca_appearance_list *)ent->value;
			if(app_list == NULL) {
				continue;
			}

			unlinked = 0;

			for(cur_app = &app_list->appearances; *cur_app != NULL;
					cur_app = tmp_app) {
				tmp_app = &(*cur_app)->next;

				switch((*cur_app)->state) {
					case KZ_SCA_APPEARANCE_STATE_ACTIVE_PENDING:
						ttl = KZ_SCA_APPEARANCE_STATE_PENDING_TTL;
						break;

					case KZ_SCA_APPEARANCE_STATE_SEIZED:
						ttl = KZ_SCA_APPEARANCE_STATE_SEIZED_TTL;
						break;

					default:
						// XXX for now just skip other appearances
						ttl = now + 60;
						break;
				}
				if((now - (*cur_app)->times.mtime) < ttl) {
					continue;
				}

				// unlink stale appearance
				app = *cur_app;
				*cur_app = (*cur_app)->next;
				tmp_app = cur_app;

				if(app) {
					kz_sca_appearance_free(app);
				}

				if(unlinked) {
					// we've already added this AoR to the NOTIFY list
					continue;
				}
				unlinked++;


				// can't notify while slot is locked. make a list of AoRs to
				// notify after unlocking.
				tmp_nl = (struct notify_list *)pkg_malloc(sizeof(struct notify_list));
				if(tmp_nl == NULL) {
					LM_ERR("kz_sca_appearance_purge_stale: failed to pkg_malloc notify list entry for %.*s\n", STR_FMT(&app_list->aor));
					continue;
				}

				tmp_nl->aor.s = (char *)pkg_malloc(app_list->aor.len);
				if(tmp_nl->aor.s == NULL) {
					LM_ERR("kz_sca_appearance_purge_stale: failed to pkg_malloc space for copy of %.*s\n", STR_FMT(&app_list->aor));
					pkg_free(tmp_nl);
					continue;
				}
				KZ_SCA_STR_COPY(&tmp_nl->aor, &app_list->aor);

				// simple insert-at-head. order doesn't matter.
				tmp_nl->next = notify_list;
				notify_list = tmp_nl;
			}
		}

		kz_sca_hash_table_unlock_index(ht, i);

		for(; notify_list != NULL; notify_list = tmp_nl) {
			tmp_nl = notify_list->next;

			SCA_DBG("kz_sca_appearance_purge_stale: notifying %.*s call-info subscribers\n", STR_FMT(&notify_list->aor));

			if(kz_sca_notify_call_info_subscribers(scam, &notify_list->aor) < 0) {
				SCA_DBG("kz_sca_appearance_purge_stale: failed to send call-info NOTIFY %.*s subscribers\n", STR_FMT(&notify_list->aor));
				// fall through, free memory anyway
			}

			if(notify_list->aor.s) {
				pkg_free(notify_list->aor.s);
			}
			pkg_free(notify_list);
		}
	}
}
