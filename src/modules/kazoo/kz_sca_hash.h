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
#ifndef KZ_SCA_HASH_H
#define KZ_SCA_HASH_H

struct _kz_sca_hash_slot;
struct _kz_sca_hash_entry
{
	void *value;
	int (*compare)(str *, void *);
	void (*description)(void *);
	void (*free_entry)(void *);
	struct _kz_sca_hash_slot *slot;
	struct _kz_sca_hash_entry *next;
};
typedef struct _kz_sca_hash_entry kz_sca_hash_entry;

struct _kz_sca_hash_slot
{
	gen_lock_t lock;
	kz_sca_hash_entry *entries;
};
typedef struct _kz_sca_hash_slot kz_sca_hash_slot;

struct _kz_sca_hash_table
{
	unsigned int size; // power of two
	kz_sca_hash_slot *slots;
};
typedef struct _kz_sca_hash_table kz_sca_hash_table;

#define kz_sca_hash_table_index_for_key(ht1, str1) \
	(get_hash1_raw((str1)->s, (str1)->len) & ((ht1)->size - 1))

#define kz_sca_hash_table_slot_for_index(ht1, idx1) &(ht1)->slots[(idx1)]

#define kz_sca_hash_table_lock_index(ht1, idx1) \
	lock_get(&(ht1)->slots[(idx1)].lock)

#define kz_sca_hash_table_unlock_index(ht1, idx1) \
	lock_release(&(ht1)->slots[(idx1)].lock)

// hash table operations
int kz_sca_hash_table_create(kz_sca_hash_table **, unsigned int);
void kz_sca_hash_table_print(kz_sca_hash_table *);
void kz_sca_hash_table_free(kz_sca_hash_table *);

void kz_sca_hash_entry_free(kz_sca_hash_entry *);

// key-value operations
int kz_sca_hash_table_slot_kv_insert_unsafe(kz_sca_hash_slot *, void *,
		int (*)(str *, void *), void (*)(void *), void (*)(void *));
int kz_sca_hash_table_slot_kv_insert(kz_sca_hash_slot *, void *,
		int (*)(str *, void *), void (*)(void *), void (*)(void *));
int kz_sca_hash_table_index_kv_insert(kz_sca_hash_table *, int, void *,
		int (*)(str *, void *), void (*)(void *), void (*)(void *));
int kz_sca_hash_table_kv_insert(kz_sca_hash_table *, str *, void *,
		int (*)(str *, void *), void (*)(void *), void (*)(void *));
void *kz_sca_hash_table_slot_kv_find_unsafe(kz_sca_hash_slot *, str *);
void *kz_sca_hash_table_slot_kv_find(kz_sca_hash_slot *, str *);
void *kz_sca_hash_table_index_kv_find_unsafe(kz_sca_hash_table *, int, str *);
void *kz_sca_hash_table_index_kv_find(kz_sca_hash_table *, int, str *);
void *kz_sca_hash_table_kv_find(kz_sca_hash_table *, str *);

kz_sca_hash_entry *kz_sca_hash_table_slot_kv_find_entry_unsafe(
		kz_sca_hash_slot *, str *);
kz_sca_hash_entry *kz_sca_hash_table_slot_unlink_entry_unsafe(
		kz_sca_hash_slot *, kz_sca_hash_entry *);

int kz_sca_hash_table_slot_kv_delete(kz_sca_hash_slot *, str *);
int kz_sca_hash_table_index_kv_delete(kz_sca_hash_table *, int, str *);
int kz_sca_hash_table_kv_delete(kz_sca_hash_table *, str *);

#endif // KZ_SCA_HASH_H
