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
#include "kz_sca_hash.h"

int kz_sca_hash_table_create(kz_sca_hash_table **ht, unsigned int size)
{
	int i;

	assert(ht != NULL);

	*ht = shm_malloc(sizeof(kz_sca_hash_table));
	if(*ht == NULL) {
		LM_ERR("Failed to shm_malloc space for hash table\n");
		return (-1);
	}

	(*ht)->size = size;
	(*ht)->slots = (kz_sca_hash_slot *)shm_malloc(size * sizeof(kz_sca_hash_slot));
	if((*ht)->slots == NULL) {
		LM_ERR("Failed to shm_malloc hash table slots\n");
		shm_free(*ht);
		*ht = NULL;
		return (-1);
	}
	memset((*ht)->slots, 0, size * sizeof(kz_sca_hash_slot));

	for(i = 0; i < (*ht)->size; i++) {
		if(lock_init(&(*ht)->slots[i].lock) == NULL) {
			LM_ERR("Failed to initialized lock in hash table slot %d\n", i);
			/* destroy locks that were already initialized */
			while(--i >= 0) {
				lock_destroy(&(*ht)->slots[i].lock);
			}
			shm_free((*ht)->slots);
			shm_free(*ht);
			*ht = NULL;
			return (-1);
		}
	}

	return (0);
}

int kz_sca_hash_table_slot_kv_insert_unsafe(kz_sca_hash_slot *slot, void *value,
		int (*e_compare)(str *, void *), void (*e_description)(void *),
		void (*e_free)(void *))
{
	kz_sca_hash_entry *new_entry;
	kz_sca_hash_entry **cur_entry;

	assert(slot != NULL);
	assert(value != NULL);
	assert(e_free != NULL);

	new_entry = (kz_sca_hash_entry *)shm_malloc(sizeof(kz_sca_hash_entry));
	if(new_entry == NULL) {
		LM_ERR("Failed to shm_malloc new hash table entry for slot %p\n", slot);
		return (-1);
	}
	new_entry->value = value;
	new_entry->compare = e_compare;
	new_entry->description = e_description;
	new_entry->free_entry = e_free;
	new_entry->slot = slot;

	cur_entry = &slot->entries;
	new_entry->next = *cur_entry;
	*cur_entry = new_entry;

	return (0);
}

int kz_sca_hash_table_slot_kv_insert(kz_sca_hash_slot *slot, void *value,
		int (*e_compare)(str *, void *), void (*e_description)(void *),
		void (*e_free)(void *))
{
	int rc;

	lock_get(&slot->lock);

	rc = kz_sca_hash_table_slot_kv_insert_unsafe(
			slot, value, e_compare, e_description, e_free);

	lock_release(&slot->lock);

	return (rc);
}

int kz_sca_hash_table_index_kv_insert(kz_sca_hash_table *ht, int slot_idx,
		void *value, int (*e_compare)(str *, void *),
		void (*e_description)(void *), void (*e_free)(void *))
{
	assert(ht != NULL);
	assert(ht->slots != NULL);
	assert(slot_idx >= 0 && slot_idx < ht->size);

	return (kz_sca_hash_table_slot_kv_insert(
			&ht->slots[slot_idx], value, e_compare, e_description, e_free));
}

int kz_sca_hash_table_kv_insert(kz_sca_hash_table *ht, str *key, void *value,
		int (*e_compare)(str *, void *), void (*e_description)(void *),
		void (*e_free)(void *))
{
	int hash_idx;
	int rc;

	assert(ht != NULL && !KZ_SCA_STR_EMPTY(key) && value != NULL);

	hash_idx = kz_sca_hash_table_index_for_key(ht, key);
	rc = kz_sca_hash_table_index_kv_insert(
			ht, hash_idx, value, e_compare, e_description, e_free);

	return (rc);
}

void *kz_sca_hash_table_slot_kv_find_unsafe(kz_sca_hash_slot *slot, str *key)
{
	kz_sca_hash_entry *e;
	void *value = NULL;

	assert(slot != NULL && !KZ_SCA_STR_EMPTY(key));

	for(e = slot->entries; e != NULL; e = e->next) {
		if(e->compare(key, e->value) == 0) {
			value = e->value;
		}
	}

	return (value);
}

void *kz_sca_hash_table_slot_kv_find(kz_sca_hash_slot *slot, str *key)
{
	void *value;

	lock_get(&slot->lock);
	value = kz_sca_hash_table_slot_kv_find_unsafe(slot, key);
	lock_release(&slot->lock);

	return (value);
}

void *kz_sca_hash_table_index_kv_find_unsafe(
		kz_sca_hash_table *ht, int slot_idx, str *key)
{
	assert(ht != NULL && !KZ_SCA_STR_EMPTY(key));
	assert(slot_idx >= 0 && slot_idx < ht->size);

	return (kz_sca_hash_table_slot_kv_find_unsafe(&ht->slots[slot_idx], key));
}

void *kz_sca_hash_table_index_kv_find(kz_sca_hash_table *ht, int slot_idx, str *key)
{
	assert(ht != NULL && !KZ_SCA_STR_EMPTY(key));
	assert(slot_idx >= 0 && slot_idx < ht->size);

	return (kz_sca_hash_table_slot_kv_find(&ht->slots[slot_idx], key));
}

void *kz_sca_hash_table_kv_find(kz_sca_hash_table *ht, str *key)
{
	int slot_idx;

	slot_idx = kz_sca_hash_table_index_for_key(ht, key);

	return (kz_sca_hash_table_index_kv_find(ht, slot_idx, key));
}

kz_sca_hash_entry *kz_sca_hash_table_slot_kv_find_entry_unsafe(
		kz_sca_hash_slot *slot, str *key)
{
	kz_sca_hash_entry *e = NULL;

	assert(slot != NULL && !KZ_SCA_STR_EMPTY(key));

	for(e = slot->entries; e != NULL; e = e->next) {
		if(e->compare(key, e->value) == 0) {
			break;
		}
	}

	return (e);
}

kz_sca_hash_entry *kz_sca_hash_table_slot_kv_find_entry(kz_sca_hash_slot *slot, str *key)
{
	kz_sca_hash_entry *e;

	lock_get(&slot->lock);
	e = kz_sca_hash_table_slot_kv_find_entry_unsafe(slot, key);
	lock_release(&slot->lock);

	return (e);
}

void kz_sca_hash_entry_free(kz_sca_hash_entry *e)
{
	assert(e != NULL);

	e->free_entry(e->value);
	shm_free(e);
}

kz_sca_hash_entry *kz_sca_hash_table_slot_unlink_entry_unsafe(
		kz_sca_hash_slot *slot, kz_sca_hash_entry *e)
{
	kz_sca_hash_entry **cur_e;

	assert(slot != NULL);
	assert(e != NULL);

	for(cur_e = &slot->entries; *cur_e != NULL; cur_e = &(*cur_e)->next) {
		if(*cur_e == e) {
			*cur_e = e->next;

			/* ensure any attempted traversal using this entry goes nowhere */
			e->next = NULL;
			e->slot = NULL;

			break;
		}
	}

	return (e);
}

int kz_sca_hash_table_slot_kv_delete_unsafe(kz_sca_hash_slot *slot, str *key)
{
	kz_sca_hash_entry *e;

	e = kz_sca_hash_table_slot_kv_find_entry_unsafe(slot, key);
	if(e == NULL) {
		return (-1);
	}

	e = kz_sca_hash_table_slot_unlink_entry_unsafe(slot, e);
	if(e) {
		e->free_entry(e->value);
		shm_free(e);
	}

	return (0);
}

int kz_sca_hash_table_slot_kv_delete(kz_sca_hash_slot *slot, str *key)
{
	int rc;

	lock_get(&slot->lock);
	rc = kz_sca_hash_table_slot_kv_delete_unsafe(slot, key);
	lock_release(&slot->lock);

	return (rc);
}

int kz_sca_hash_table_index_kv_delete(kz_sca_hash_table *ht, int slot_idx, str *key)
{
	return (kz_sca_hash_table_slot_kv_delete(&ht->slots[slot_idx], key));
}

int kz_sca_hash_table_kv_delete(kz_sca_hash_table *ht, str *key)
{
	int slot_idx;

	slot_idx = kz_sca_hash_table_index_for_key(ht, key);

	return (kz_sca_hash_table_index_kv_delete(ht, slot_idx, key));
}

static void kz_sca_hash_slot_print(kz_sca_hash_slot *hs)
{
	kz_sca_hash_entry *e;

	for(e = hs->entries; e != NULL; e = e->next) {
		if(e->description != NULL) {
			e->description(e->value);
		} else {
			SCA_DBG("0x%p\n", e->value);
		}
	}
}

void kz_sca_hash_table_print(kz_sca_hash_table *ht)
{
	unsigned int i;

	for(i = 0; i < ht->size; i++) {
		SCA_DBG("SLOT %d:\n", i);
		kz_sca_hash_slot_print(&ht->slots[i]);
	}
}

void kz_sca_hash_table_free(kz_sca_hash_table *ht)
{
	kz_sca_hash_entry *e, *e_tmp;
	unsigned int i;

	if(ht == NULL) {
		return;
	}

	for(i = 0; i < ht->size; i++) {
		for(e = ht->slots[i].entries; e != NULL; e = e_tmp) {
			e_tmp = e->next;

			e->free_entry(e->value);

			shm_free(e);
		}

		lock_destroy(&ht->slots[i].lock);
	}

	shm_free(ht->slots);
	shm_free(ht);
}
