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
#include "../../core/str.h"

#ifndef KZ_SCA_APPEARANCE_H
#define KZ_SCA_APPEARANCE_H

#include "kz_sca_dialog.h"
#include "kz_sca_call_info_extended.h"

enum
{
	KZ_SCA_APPEARANCE_STATE_IDLE = 0,
	KZ_SCA_APPEARANCE_STATE_SEIZED,
	KZ_SCA_APPEARANCE_STATE_PROGRESSING,
	KZ_SCA_APPEARANCE_STATE_ALERTING,
	KZ_SCA_APPEARANCE_STATE_ACTIVE_PENDING,
	KZ_SCA_APPEARANCE_STATE_ACTIVE,
	KZ_SCA_APPEARANCE_STATE_HELD,
	KZ_SCA_APPEARANCE_STATE_HELD_PRIVATE,
	KZ_SCA_APPEARANCE_STATE_UNKNOWN = 0xff,
};

#define kz_sca_appearance_is_held(app1)                       \
	((app1)                                                \
			&& ((app1)->state == KZ_SCA_APPEARANCE_STATE_HELD \
					   || (app1)->state == KZ_SCA_APPEARANCE_STATE_HELD_PRIVATE))

enum
{
	KZ_SCA_APPEARANCE_FLAG_DEFAULT = 0,
	KZ_SCA_APPEARANCE_FLAG_OWNER_PENDING = (1 << 0),
	KZ_SCA_APPEARANCE_FLAG_CALLEE_PENDING = (1 << 1),
};

enum
{
	KZ_SCA_APPEARANCE_OK = 0,
	KZ_SCA_APPEARANCE_ERR_NOT_IN_USE = 0x1001,
	KZ_SCA_APPEARANCE_ERR_INDEX_INVALID = 0x1002,
	KZ_SCA_APPEARANCE_ERR_INDEX_UNAVAILABLE = 0x1004,
	KZ_SCA_APPEARANCE_ERR_MALLOC = 0x1008,
	KZ_SCA_APPEARANCE_ERR_UNKNOWN = 0x1f00,
};
#define KZ_SCA_APPEARANCE_INDEX_UNAVAILABLE -2

/*
 * maximum lifetime of an active, pending appearance.
 * enough to allow retransmissions of the caller's
 * ACK. on receipt of the caller's ACK, we promote
 * the KZ_SCA callee's state to active.
 */
enum
{
	// Polycoms aggressively resubscribe line-seizes, give them time
	KZ_SCA_APPEARANCE_STATE_SEIZED_TTL = 120,

	// enough time to allow retransmissions (~32s)
	KZ_SCA_APPEARANCE_STATE_PENDING_TTL = 35,
};

extern const str KZ_SCA_APPEARANCE_INDEX_STR;
extern const str KZ_SCA_APPEARANCE_STATE_STR;
extern const str KZ_SCA_APPEARANCE_URI_STR;

extern const str KZ_SCA_APPEARANCE_STATE_STR_IDLE;
extern const str KZ_SCA_APPEARANCE_STATE_STR_SEIZED;
extern const str KZ_SCA_APPEARANCE_STATE_STR_PROGRESSING;
extern const str KZ_SCA_APPEARANCE_STATE_STR_ALERTING;
extern const str KZ_SCA_APPEARANCE_STATE_STR_ACTIVE;
extern const str KZ_SCA_APPEARANCE_STATE_STR_HELD;
extern const str KZ_SCA_APPEARANCE_STATE_STR_HELD_PRIVATE;

struct _kz_sca_appearance_times
{
	// time of appearance creation
	time_t ctime;

	// time of last appearance state change
	time_t mtime;

	// time of last end-to-end activity
	time_t atime;
};
typedef struct _kz_sca_appearance_times kz_sca_appearance_times;

struct _kz_sca_appearance_list;
struct _kz_sca_appearance
{
	int index;
	int state;
	str uri;
	str switch_uri;
	str ccvs;
	kz_sca_presence_direction_t direction;
	str aliases;
	str call_interaction_id;

	int flags;

	str owner;
	str callee;
	kz_sca_dialog dialog;
	kz_sca_appearance_times times;

	str prev_owner;
	str prev_callee;
	kz_sca_dialog prev_dialog;

	struct _kz_sca_appearance_list *appearance_list;
	struct _kz_sca_appearance *next;
};
typedef struct _kz_sca_appearance kz_sca_appearance_t;

struct _kz_sca_appearance_list
{
	str aor;
	int appearance_count;
	kz_sca_appearance_t *appearances;
};
typedef struct _kz_sca_appearance_list kz_sca_appearance_list;

const char * kz_sca_appearance_state_to_string(int state);
void kz_sca_appearance_state_to_str(int, str *);
int kz_sca_appearance_state_from_str(str *);
int kz_sca_appearance_state_from_string(const char * state_string);

kz_sca_appearance_t *kz_sca_appearance_seize_index_unsafe(kz_sca_mod *, str *, str *, int, int, int *);
int kz_sca_appearance_seize_index(kz_sca_mod *, str *, int, str *);
int kz_sca_appearance_seize_next_available_index(kz_sca_mod *, str *, str *);
kz_sca_appearance_t *kz_sca_appearance_seize_next_available_unsafe(kz_sca_mod *, str *, str *, int);
void kz_sca_appearance_update_state_unsafe(kz_sca_appearance_t *, int);
int kz_sca_appearance_update_switch_uri_unsafe(kz_sca_appearance_t *, str *);
int kz_sca_appearance_update_ccvs_unsafe(kz_sca_appearance_t *, str *);
int kz_sca_appearance_update_aliases_unsafe(kz_sca_appearance_t *app, str *aliases);
int kz_sca_appearance_update_call_interaction_id_unsafe(kz_sca_appearance_t *app, str *call_interaction_id);

int kz_sca_appearance_update_direction_unsafe(kz_sca_appearance_t *app, kz_sca_presence_direction_t direction);
int kz_sca_appearance_update_owner_unsafe(kz_sca_appearance_t *, str *);
int kz_sca_appearance_update_callee_unsafe(kz_sca_appearance_t *, str *);
int kz_sca_appearance_update_dialog_unsafe(kz_sca_appearance_t *, str *, str *, str *);
int kz_sca_appearance_update_unsafe(kz_sca_appearance_t *, int, str *, str *, kz_sca_dialog *, str *, str *);
int kz_sca_appearance_update_index(kz_sca_mod *, str *, int, int, str *, str *, kz_sca_dialog *);
int kz_sca_appearance_release_index(kz_sca_mod *, str *, int);
int kz_sca_appearance_owner_release_all(str *, str *);
int kz_sca_appearance_state_for_index(kz_sca_mod *, str *, int);
kz_sca_appearance_t *kz_sca_appearance_for_index_unsafe(kz_sca_mod *, str *, int, int);
kz_sca_appearance_t *kz_sca_appearance_for_dialog_unsafe(kz_sca_mod *, str *, kz_sca_dialog *, int);
kz_sca_appearance_t *kz_sca_appearance_for_tags_unsafe(kz_sca_mod *, str *, str *, str *, str *, int);

int kz_sca_appearance_register(kz_sca_mod *, str *);
int kz_sca_appearance_unregister(kz_sca_mod *, str *);
void kz_sca_appearance_list_insert_appearance(kz_sca_appearance_list *, kz_sca_appearance_t *);
kz_sca_appearance_t *kz_sca_appearance_list_unlink_index(kz_sca_appearance_list *, int);
int kz_sca_appearance_list_unlink_appearance(kz_sca_appearance_list *, kz_sca_appearance_t **);
kz_sca_appearance_t *kz_sca_appearance_unlink_by_tags(kz_sca_mod *, str *, str *, str *, str *);

kz_sca_appearance_t *kz_sca_appearance_create(int, str *);
void kz_sca_appearance_free(kz_sca_appearance_t *);

int kz_sca_uri_is_shared_appearance(kz_sca_mod *, str *);
int kz_sca_uri_lock_shared_appearance(kz_sca_mod *, str *);
int kz_sca_uri_lock_if_shared_appearance(kz_sca_mod *, str *, int *);

void kz_sca_appearance_purge_stale(unsigned int, void *);
#endif // KZ_SCA_APPEARANCE_H
