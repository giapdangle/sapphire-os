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


#ifndef _MEMORY_H
#define _MEMORY_H

#include "system.h"

#define MAX_MEM_HANDLES         256
#define MEM_HEAP_SIZE           8192

#define MEM_MAX_STACK           2048
#define MEM_STACK_THRESHOLD_0   1024
#define MEM_STACK_THRESHOLD_1   1536

// defragmenter will only run after the amount of dirty space exceeds this threshold
#define MEM_DEFRAG_THRESHOLD    1024

//#define ENABLE_EXTENDED_VERIFY
//#define ENABLE_RECORD_CREATOR

typedef int16_t mem_handle_t;

typedef struct{
	uint16_t size;
	mem_handle_t handle;
    #ifdef ENABLE_RECORD_CREATOR
    uint16_t creator_address;
    #endif
} mem_block_header_t;

#define MEM_SIZE_DIRTY_MASK 0x8000

// memory run time data structure
// used for internal record keeping, and can also be accessed externally as a read only
typedef struct{
	uint16_t handles_used;
	uint16_t free_space;
	uint16_t used_space;
	uint16_t dirty_space;
	uint16_t data_space;
	uint16_t peak_usage;
} mem_rt_data_t;

void mem2_v_init( void );

mem_block_header_t mem2_h_get_header( uint16_t index );

bool mem2_b_verify_handle( mem_handle_t handle );
mem_handle_t mem2_h_alloc( uint16_t size );

#ifdef ENABLE_EXTENDED_VERIFY
    void _mem2_v_free( mem_handle_t handle, FLASH_STRING_T file, int line );
    uint16_t _mem2_u16_get_size( mem_handle_t handle, FLASH_STRING_T file, int line );
    void *_mem2_vp_get_ptr( mem_handle_t handle, FLASH_STRING_T file, int line );
    
    #define mem2_v_free(handle)         _mem2_v_free(handle, FLASH_STRING( __FILE__ ), __LINE__ )
    #define mem2_u16_get_size(handle)   _mem2_u16_get_size(handle, FLASH_STRING( __FILE__ ), __LINE__ )
    #define mem2_vp_get_ptr(handle)     _mem2_vp_get_ptr(handle, FLASH_STRING( __FILE__ ), __LINE__ )

#else
    void mem2_v_free( mem_handle_t handle );
    uint16_t mem2_u16_get_size( mem_handle_t handle );
    void *mem2_vp_get_ptr( mem_handle_t handle );
#endif

void *mem2_vp_get_ptr_fast( mem_handle_t handle );

void mem2_v_check_canaries( void );
uint16_t mem_u16_get_stack_usage( void );
uint16_t mem2_u16_get_handles_used( void );
void mem2_v_get_rt_data( mem_rt_data_t *rt_data );
uint16_t mem2_u16_get_free( void );
void mem2_v_collect_garbage( void );


#endif

