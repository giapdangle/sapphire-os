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

#include "system.h"
#include "memory.h"

#include "list.h"


void list_v_init( list_t *list ){
    
    list->head = -1;
    list->tail = -1;
}


list_node_t list_ln_create_node( void *data, uint16_t len ){

    // create memory object
    mem_handle_t h = mem2_h_alloc( ( sizeof(list_node_state_t) - 1 ) + len );

    // create handle
    if( h < 0 ){
    
		return -1;
    }

    // get state pointer and initialize
	list_node_state_t *state = mem2_vp_get_ptr( h );
	
    state->prev = -1; 
    state->next = -1; 
    
    // check if data is provided
    if( data != 0 ){
        
        // copy data into netmsg object
        memcpy( &state->data, data, len );
    }

    return h;
}

// NOTE: this function does NOT remove the node from whatever list it was in.
// that needs to be done BEFORE calling this function!
void list_v_release_node( list_node_t node ){
    
    mem2_v_free( node );
}

uint8_t list_u8_count( list_t *list ){
    
    uint8_t count = 0;

    list_node_t node = list->head;
    
    while( node >= 0 ){
        
        count++;
        
        node = list_ln_next( node );
    }

    return count;
}

uint16_t list_u16_size( list_t *list ){
    
    uint16_t size = 0;

    list_node_t node = list->head;
    
    while( node >= 0 ){
        
        size += list_u16_node_size( node );
        
        node = list_ln_next( node );
    }

    return size;
}

uint16_t list_u16_node_size( list_node_t node ){
    
    return ( mem2_u16_get_size( node ) - ( sizeof(list_node_state_t) - 1 ) );
}

void *list_vp_get_data( list_node_t node ){
    
	list_node_state_t *state = mem2_vp_get_ptr( node );
    
    return &state->data;
}

void list_v_insert_after( list_t *list, list_node_t node, list_node_t new_node ){
    
    // get node states
    list_node_state_t *state = mem2_vp_get_ptr( node );
    list_node_state_t *new_state = mem2_vp_get_ptr( new_node );
    
    new_state->prev = node;
    new_state->next = state->next;

    if( state->next < 0 ){
        
        list->tail = new_node;
    }
    else{
        
        list_node_state_t *next_state = mem2_vp_get_ptr( state->next );
        
        next_state->prev = new_node;
    }

    state->next = new_node;
}

void list_v_insert_tail( list_t *list, list_node_t node ){
    
    // get node state
    list_node_state_t *state = mem2_vp_get_ptr( node );

    // check if list is empty
    if( list->tail < 0 ){
        
        list->head = node;
        list->tail = node;

        state->prev = -1;
        state->next = -1;
    }
    else{
        
        list_v_insert_after( list, list->tail, node );
    }
}

void list_v_insert_head( list_t *list, list_node_t node ){
    
    // get node state
    list_node_state_t *state = mem2_vp_get_ptr( node );

    // check if list is empty
    if( list->head < 0 ){
        
        list->head = node;
        list->tail = node;

        state->prev = -1;
        state->next = -1;
    }
    else{
        
        // insert at head
        list_node_state_t *head = mem2_vp_get_ptr( list->head );
        
        head->prev = node;

        state->prev = -1;
        state->next = list->head;
        
        list->head = node;
    }
}

void list_v_remove( list_t *list, list_node_t node ){
    
    // get node state
    list_node_state_t *state = mem2_vp_get_ptr( node );
    
    if( state->prev < 0 ){
        
        list->head = state->next;
    }
    else{
        
        // get previous state 
        list_node_state_t *prev_state = mem2_vp_get_ptr( state->prev );
        
        prev_state->next = state->next;
    }
    
    if( state->next < 0 ){
        
        list->tail = state->prev;
    }
    else{
        
        // get next state 
        list_node_state_t *next_state = mem2_vp_get_ptr( state->next );
        
        next_state->prev = state->prev;
    }
}

list_node_t list_ln_remove_tail( list_t *list ){
    
    list_node_t tail = list->tail;
    
    if( tail >= 0 ){
        
        list_v_remove( list, list->tail );
    }

    return tail;
}

list_node_t list_ln_next( list_node_t node ){
    
    // get node state
    list_node_state_t *state = mem2_vp_get_ptr( node );
    
    return state->next;
}

list_node_t list_ln_prev( list_node_t node ){
    
    // get node state
    list_node_state_t *state = mem2_vp_get_ptr( node );
    
    return state->prev;
}

list_node_t list_ln_index( list_t *list, uint16_t index ){
    
    list_node_t node = list->head;
    
    while( ( node >= 0 ) && ( index > 0 ) ){
        
        list_node_t next = list_ln_next( node );

        node = next;
        index--;
    }

    return node;
}

bool list_b_is_empty( list_t *list ){
    
    bool empty = ( list->head < 0 );

    return empty;
}

// release every object in given list
void list_v_destroy( list_t *list ){
    
    list_node_t node = list->head;
    
    while( node >= 0 ){
        
        list_node_t next = list_ln_next( node );
        
        list_v_release_node( node );

        node = next;
    }

    list_v_init( list );
}

uint16_t list_u16_flatten( list_t *list, uint16_t pos, void *dest, uint16_t len ){
    
    uint16_t current_pos = 0;
    uint16_t data_copied = 0;

    // seek to requested position
    list_node_t node = list->head;
    
    while( ( node >= 0 ) && ( len > 0 ) ){
        
        uint16_t node_size = list_u16_node_size( node );
        
        if( ( pos >= current_pos ) &&
            ( pos < ( current_pos + node_size ) ) ){
            
            uint16_t offset = pos - current_pos;
            uint16_t copy_len = node_size - offset;
            
            if( copy_len > len ){
                
                copy_len = len;
            }

            memcpy( dest, list_vp_get_data( node ) + offset, copy_len );

            data_copied += copy_len;
            dest += copy_len;
            pos += copy_len;
            len -= copy_len;
        }
        
        current_pos += node_size;
        node = list_ln_next( node );
    }
    
    return data_copied;
}


