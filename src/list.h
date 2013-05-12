/* 
 * <license>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *
 * This file is part of the Sapphire Operating System
 *
 * Copyright 2013 Sapphire Open Systems
 *
 * </license>
 */

#ifndef _LIST_H
#define _LIST_H

#include "memory.h"

typedef mem_handle_t list_node_t;

typedef struct{
    list_node_t prev;
    list_node_t next;
    uint8_t     data; // first byte of data
} list_node_state_t;

typedef struct{
    list_node_t head;
    list_node_t tail;
} list_t;


void list_v_init( list_t *list );

list_node_t list_ln_create_node( void *data, uint16_t len );
void list_v_release_node( list_node_t node );
uint8_t list_u8_count( list_t *list );
uint16_t list_u16_size( list_t *list );
uint16_t list_u16_node_size( list_node_t node );
void *list_vp_get_data( list_node_t node );

void list_v_insert_after( list_t *list, list_node_t node, list_node_t new_node );
void list_v_insert_tail( list_t *list, list_node_t node );
void list_v_insert_head( list_t *list, list_node_t node );
void list_v_remove( list_t *list, list_node_t node );
list_node_t list_ln_remove_tail( list_t *list );
list_node_t list_ln_next( list_node_t node );
list_node_t list_ln_prev( list_node_t node );
list_node_t list_ln_index( list_t *list, uint16_t index );

bool list_b_is_empty( list_t *list );

void list_v_destroy( list_t *list );

uint16_t list_u16_flatten( list_t *list, uint16_t pos, void *dest, uint16_t len );

#endif

