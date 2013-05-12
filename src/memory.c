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


/*

Dynamic Memory Pool Implementation

*/	


/*
Notes:

Memory block format: header - data - canary

Heap format: used and dirty blocks - free space

Memory overhead is 4 bytes per handle used

*/



#include "timers.h"
#include "threading.h"
#include "system.h"
#include "statistics.h"
#include "keyvalue.h"

#include "memory.h"

#include <string.h>

//#define NO_LOGGING
#include "logging.h"


static void *handles[MAX_MEM_HANDLES];

static uint8_t heap[MEM_HEAP_SIZE];

static void *free_space_ptr;

static mem_rt_data_t mem_rt_data;
static uint16_t stack_usage;

// KV:
static int8_t mem_i8_kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len )
{

    if( op == KV_OP_GET ){
        
        if( id == KV_ID_MAX_MEM_HANDLES ){
            
            uint16_t a = MAX_MEM_HANDLES;
            memcpy( data, &a, sizeof(a) );
        }
        else if( id == KV_ID_MAX_MEM_STACK ){
            
            uint16_t a = MEM_MAX_STACK;
            memcpy( data, &a, sizeof(a) );
        }
        else if( id == KV_ID_MEM_HEAP_SIZE ){
            
            uint16_t a = MEM_HEAP_SIZE;
            memcpy( data, &a, sizeof(a) );
        }
    }

    return 0;
}

KV_SECTION_META kv_meta_t mem_info_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_MEM_HANDLES,     SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &mem_rt_data.handles_used, 0,  "mem_handles" },
    { KV_GROUP_SYS_INFO, KV_ID_MAX_MEM_HANDLES, SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  0, mem_i8_kv_handler,          "mem_max_handles" },
    { KV_GROUP_SYS_INFO, KV_ID_MEM_STACK,       SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &stack_usage,              0,  "mem_stack" },
    { KV_GROUP_SYS_INFO, KV_ID_MAX_MEM_STACK,   SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  0, mem_i8_kv_handler,          "mem_max_stack" },
    { KV_GROUP_SYS_INFO, KV_ID_MEM_HEAP_SIZE,   SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  0, mem_i8_kv_handler,          "mem_heap_size" },
    { KV_GROUP_SYS_INFO, KV_ID_MEM_FREE,        SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &mem_rt_data.free_space,   0,  "mem_free_space" },
    { KV_GROUP_SYS_INFO, KV_ID_MEM_PEAK,        SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &mem_rt_data.peak_usage,   0,  "mem_peak_usage" },
};


#define CANARY_VALUE 0x47

#define MEM_BLOCK_SIZE( header ) ( sizeof(mem_block_header_t) + \
								( ( ~MEM_SIZE_DIRTY_MASK ) & header->size ) + \
								1 )

#define CANARY_PTR( header ) ( ( uint8_t * )header + \
							  MEM_BLOCK_SIZE( header ) - 1 )


#define SWIZZLE_VALUE 1

static void release_block( mem_handle_t handle );

PT_THREAD( mem2_garbage_collector_thread( pt_t *pt, void *state ) );

static mem_handle_t swizzle( mem_handle_t handle ){
	
	return handle + SWIZZLE_VALUE;
}

static mem_handle_t unswizzle( mem_handle_t handle ){
	
	return handle - SWIZZLE_VALUE;
}


static uint8_t generate_canary( mem_block_header_t *header ){
	
	return 	CANARY_VALUE;
}


static bool is_dirty( mem_block_header_t *header ){
    
    if( ( header->size & MEM_SIZE_DIRTY_MASK ) != 0 ){
        
        return TRUE;
    }
    
    return FALSE;
}

static void set_dirty( mem_block_header_t *header ){
    
    header->size |= MEM_SIZE_DIRTY_MASK;
}

#ifndef ENABLE_EXTENDED_VERIFY
static void verify_handle( mem_handle_t handle ){
	
    ASSERT_MSG( handles[handle] != 0, "Invalid handle" );
    ASSERT( handle >= 0 );
    
	mem_block_header_t *header = handles[handle];
	uint8_t *canary = CANARY_PTR( header );

	ASSERT_MSG( *canary == generate_canary( header ), "Invalid canary" );
	ASSERT_MSG( is_dirty( header ) == FALSE, "Memory block is marked as dirty" );
}
#endif


void stack_fill( void ) __attribute__ ((naked)) __attribute__ ((section (".init1")));

void stack_fill( void ){
    
    __asm volatile(
        "ldi r30, lo8(__stack)\n"
        "ldi r31, hi8(__stack)\n"
        "ldi r24, lo8(2048)\n" // MEM_MAX_STACK
        "ldi r25, hi8(2048)\n" // MEM_MAX_STACK
        "ldi r16, 0x47\n" // CANARY
        ".loop:\n"
        "   st -Z, r16\n"
        "   sbiw r24, 1\n"
        "   brne .loop\n"
    );
}

extern uint8_t __stack;

uint16_t stack_count( void ){
    
    uint16_t count = 0;
    uint8_t *stack = &__stack - MEM_MAX_STACK;
    
    while( count < MEM_MAX_STACK ){
        
        if( *stack != 0x47 ){
            
            break;
        }
        
        stack++;
        count++;
    }   
    
    return MEM_MAX_STACK - count;
}

void mem2_v_init( void ){
    
	mem_rt_data.free_space = MEM_HEAP_SIZE;
	free_space_ptr = heap;
	
	mem_rt_data.used_space = 0;
	mem_rt_data.data_space = 0;
	mem_rt_data.dirty_space = 0;
	
	for( uint16_t i = 0; i < MAX_MEM_HANDLES; i++ ){
		
		handles[i] = 0;
	}	
	
	mem_rt_data.handles_used = 0;
	
	thread_t_create( mem2_garbage_collector_thread,
                     PSTR("mem2_defrag"),
                     0,
                     0 );
}

// for debug only, returns a copy of the header at given index
mem_block_header_t mem2_h_get_header( uint16_t index ){
    
    mem_block_header_t header_copy;
    memset( &header_copy, 0, sizeof(header_copy) );
    
    // check if block exists
    if( handles[index] != 0 ){
	
        mem_block_header_t *block_header = handles[index];
        
        memcpy( &header_copy, block_header, sizeof(header_copy) );
    }

    return header_copy;
}

// external API for verifying a handle.
// used for checking for bad handles and asserting at the
// source file instead of within the memory module, where it is difficult
// to find the source of the problem.
// returns TRUE if handle is valid
bool mem2_b_verify_handle( mem_handle_t handle ){
    
    // check if handle is negative
    if( handle < 0 ){
        
        sys_v_set_error( SYS_ERR_INVALID_HANDLE );
        
        return FALSE;
    }
    
    // unswizzle the handle
    handle = unswizzle(handle);
    
    if( handles[handle] == 0 ){
        
        sys_v_set_error( SYS_ERR_HANDLE_UNALLOCATED );
        
        return FALSE;
    }
    
    // get header and canary
    mem_block_header_t *header = handles[handle];
	uint8_t *canary = CANARY_PTR( header );
    
    if( *canary != generate_canary( header ) ){
        
        sys_v_set_error( SYS_ERR_INVALID_CANARY );
        
        return FALSE;
    }
    
    if( is_dirty( header ) == TRUE ){
        
        sys_v_set_error( SYS_ERR_MEM_BLOCK_IS_DIRTY );
        
        return FALSE;
    }
    
    return TRUE;
}

// attempt to allocate a memory block of a specified size
// returns -1 if the allocation failed.
mem_handle_t mem2_h_alloc( uint16_t size ){
    
    mem_handle_t handle = -1;

	// check if there is free space available
	if( ( size > MEM_HEAP_SIZE ) ||
        ( mem_rt_data.free_space < ( size + sizeof(mem_block_header_t) + 1 ) ) ){
		
		// allocation failed
	    goto failed;
    }
	
	// get a handle
	for( handle = 0; handle < MAX_MEM_HANDLES; handle++ ){
		
		if( handles[handle] == 0 ){
			
			mem_rt_data.handles_used++;
			
			break;
		}	
	}	
	
	// if a handle was not found
	if( handle >= MAX_MEM_HANDLES ){
		
		// handle allocation failed
		goto failed;
	}
	
	// create the memory block
	handles[handle] = free_space_ptr;
	
	mem_block_header_t *header = handles[handle];
	
	header->size = size;
	header->handle = handle;

    #ifdef ENABLE_RECORD_CREATOR
    // notice we multiply the address by 2 (left shift 1) to get the byte address
    header->creator_address = (uint16_t)thread_p_get_function( thread_t_get_current_thread() ) << 1;
	#endif

	// set data space
	mem_rt_data.data_space += size;
	
	// set canary
	uint8_t *canary = CANARY_PTR( header );
	
	*canary = generate_canary( header );
	
	// adjust free space
	free_space_ptr += MEM_BLOCK_SIZE( header );
	
	mem_rt_data.free_space -= MEM_BLOCK_SIZE( header );
	
	ASSERT_MSG( mem_rt_data.free_space <= MEM_HEAP_SIZE, "Free space invalid!" ); 
	
	// adjust used space counter
	mem_rt_data.used_space += MEM_BLOCK_SIZE( header );
	
    stats_v_increment( STAT_MEM_ALLOCATIONS );
	
	// adjust peak usage state
	if( mem_rt_data.peak_usage < mem_rt_data.used_space ){
		
        mem_rt_data.peak_usage = mem_rt_data.used_space;
	}
    
    handle = swizzle(handle);
    
    return handle;

failed:
        
    stats_v_increment( STAT_MEM_FAILED_ALLOCATIONS );

    sys_v_set_warnings( SYS_WARN_MEM_FULL );

	return -1;
}

// free the memory associated with the given handle
#ifdef ENABLE_EXTENDED_VERIFY
void _mem2_v_free( mem_handle_t handle, FLASH_STRING_T file, int line ){

    // check validity of handle and assert if there is a failure.
    // this overrides the system based assert so we can insert the file and line
    // from the caller.
    if( mem2_b_verify_handle( handle ) == FALSE ){
        
        assert( 0, file, line );
    }

#else
void mem2_v_free( mem_handle_t handle ){
#endif

	handle = unswizzle(handle);
	
	if( handle >= 0 ){
	
        #ifndef ENABLE_EXTENDED_VERIFY
		verify_handle( handle );
        #endif
			
		release_block( handle );
	}
}

static void release_block( mem_handle_t handle ){

	// get pointer to the header
	mem_block_header_t *header = handles[handle];
	
	// clear the handle
	handles[handle] = 0;
	
	mem_rt_data.handles_used--;
	
	// decrement data space used
	mem_rt_data.data_space -= header->size;
	
	// set the flags to dirty so the defragmenter can pick it up
	set_dirty( header );
	
	// increment dirty space counter and decrement used space counter
	mem_rt_data.dirty_space += MEM_BLOCK_SIZE( header );
	mem_rt_data.used_space -= MEM_BLOCK_SIZE( header );
	
    stats_v_increment( STAT_MEM_FREES );
}
#ifdef ENABLE_EXTENDED_VERIFY
uint16_t _mem2_u16_get_size( mem_handle_t handle, FLASH_STRING_T file, int line ){

    // check validity of handle and assert if there is a failure.
    // this overrides the system based assert so we can insert the file and line
    // from the caller.
    if( mem2_b_verify_handle( handle ) == FALSE ){
        
        assert( 0, file, line );
    }
    
#else
uint16_t mem2_u16_get_size( mem_handle_t handle ){
#endif
    
	handle = unswizzle(handle);
	
    #ifndef ENABLE_EXTENDED_VERIFY
	verify_handle( handle );
    #endif
	
	// get pointer to the header
	mem_block_header_t *header = handles[handle];
	
	return header->size;
}

// Get a pointer to the memory at given handle.
// NOTES: 
// This function is dangerous, but the fact is it is necessary to allow 
// applications to map structs to memory without copying to a temp variable.
// IMPORTANT!!! The pointer returned by this function is only valid until the
// next time the garbage collector runs, IE, if the application thread yields
// to the scheduler, it needs to call this function again to get a fresh
// pointer.  Otherwise the garbage collector may move the application's memory
// and the app's old pointer will point to invalid memory.
#ifdef ENABLE_EXTENDED_VERIFY
void *_mem2_vp_get_ptr( mem_handle_t handle, FLASH_STRING_T file, int line ){

    // check validity of handle and assert if there is a failure.
    // this overrides the system based assert so we can insert the file and line
    // from the caller.
    if( mem2_b_verify_handle( handle ) == FALSE ){
        
        assert( 0, file, line );
    }
    
#else
void *mem2_vp_get_ptr( mem_handle_t handle ){
#endif
    
	handle = unswizzle(handle);
    
	#ifndef ENABLE_EXTENDED_VERIFY
	verify_handle( handle );
	#endif
    
	return handles[handle] + sizeof( mem_block_header_t );
}

void *mem2_vp_get_ptr_fast( mem_handle_t handle ){

	handle = unswizzle(handle);
    
	return handles[handle] + sizeof( mem_block_header_t );
}

// check the canaries for all allocated handles.
// this function will assert on any failures.
void mem2_v_check_canaries( void ){
	
	for( uint16_t i = 0; i < MAX_MEM_HANDLES; i++ ){
		
		// if the handle is allocated
		if( handles[i] != 0 ){
			
			// get pointer to the header
			mem_block_header_t *header = handles[i];
			
			uint8_t *canary = CANARY_PTR( header );
			
			// check the canary
			ASSERT_MSG( *canary == generate_canary( header ), "Invalid canary!" );
		}
	}
}

uint16_t mem_u16_get_stack_usage( void ){
    
    return stack_usage;
}

uint8_t mem2_u8_get_handles_used( void ){
	
	return mem_rt_data.handles_used;
}

void mem2_v_get_rt_data( mem_rt_data_t *rt_data ){
	
	*rt_data = mem_rt_data;
}

// return amount of free memory available
uint16_t mem2_u16_get_free( void ){
    
    return mem_rt_data.free_space;
}

void mem2_v_collect_garbage( void ){
    
	pt_t pt;
	PT_INIT( &pt );
	
	mem2_garbage_collector_thread( &pt, 0 );
}

PT_THREAD( mem2_garbage_collector_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  		
	
	while(1){
		
		THREAD_WAIT_WHILE( pt, mem_rt_data.dirty_space < MEM_DEFRAG_THRESHOLD );
        
		mem_block_header_t *clean;
		mem_block_header_t *next_block;
		mem_block_header_t *dirty;
		
		clean = ( mem_block_header_t * )heap;
		next_block = ( mem_block_header_t * )heap;
		dirty = ( mem_block_header_t * )heap;
		
		// search for a dirty block (loop while clean blocks)
		while( ( is_dirty( dirty ) == FALSE ) &&
				( dirty < ( mem_block_header_t * )free_space_ptr ) ){
			
			dirty = ( void * )dirty + MEM_BLOCK_SIZE( dirty );
		}
		
		// clean pointer needs to lead dirty pointer
		if( clean < dirty ){
			
			clean = dirty;
		}
		
		while( clean < ( mem_block_header_t * )free_space_ptr ){
		
			// search for a clean block (loop while dirty)
			// (couldn't find anymore clean blocks to move, so there should be
			// no clean blocks between the dirty pointer and the free pointer)
			while( is_dirty( clean ) == TRUE ){
				
				clean = ( void * )clean + MEM_BLOCK_SIZE( clean );
				
				// if the free space is reached, the routine is finished
				if( clean >= ( mem_block_header_t * )free_space_ptr ){
					
					goto done_defrag;
				}
			}
			
			// get next block
			next_block = ( void * )clean + MEM_BLOCK_SIZE( clean );
		
			// switch the handle from the old block to the new block
			handles[clean->handle] = dirty;
            // NOTE:
            // Could do this instead, and save the need to store the handle index
            // in the block, saving 2 bytes per object.
            /*
            for( uint16_t i = 0; i < MAX_MEM_HANDLES; i++ ){
                
                if( handles[i] == clean ){

			        handles[i] = dirty;
                    break;
                }
            }
            */
			
			// copy the clean block to the dirty block pointer
			memcpy( dirty, clean, MEM_BLOCK_SIZE( clean ) );

			// increment dirty pointer
			dirty = ( void * )dirty + MEM_BLOCK_SIZE( dirty );	
			
			// assign clean pointer to next block
			clean = next_block;
		}
		
	done_defrag:
		
        stats_v_increment( STAT_MEM_DEFRAGS );
		
		// there should be no clean blocks between the dirty and free pointers,
		// set the free pointer to the dirty pointer
		free_space_ptr = dirty;
		
		// the dirty space is now free
		mem_rt_data.free_space += mem_rt_data.dirty_space;
		
		// no more dirty space
		mem_rt_data.dirty_space = 0;
        
        // run canary check
        mem2_v_check_canaries();
        
        uint16_t old_stack_usage = stack_usage;

        // get stack size
        stack_usage = stack_count();

        // check stack usage
        if( ( old_stack_usage <= MEM_STACK_THRESHOLD_0 ) &&
            ( stack_usage > MEM_STACK_THRESHOLD_0 ) ){
            
            log_v_info_P( PSTR("StackUsage:%d"), stack_usage );
        }
        else if( ( old_stack_usage <= MEM_STACK_THRESHOLD_1 ) &&
                 ( stack_usage > MEM_STACK_THRESHOLD_1 ) ){
            
            log_v_warn_P( PSTR("StackUsage:%d"), stack_usage );
        }

        // assert if the stack is blown up
        ASSERT( stack_usage < MEM_MAX_STACK );
	}
	
PT_END( pt );
}




