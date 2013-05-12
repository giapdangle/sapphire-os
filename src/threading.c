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



#include "cpu.h"

#include "system.h"
#include "keyvalue.h"

#include "timers.h"
#include "threading.h"
#include "memory.h"
#include "list.h"
#include "fs.h"

#include "logging.h"

#include <string.h>

/*
Basic thread control:
Wait & Yield

Wait - Thread waits on a given condition.  The processor is allowed to go to 
sleep during the wait, which means the thread might not run again until the
next hardware interrupt.  This is best used for low priority and non time
critical functions.

Yield - Thread yields to allow other threads to run.  This implies the thread
is not finished processing, and as such, will be guaranteed to be run again
before sleeping the processor.


*/

// thread state storage
static list_t thread_list;

// currently running thread
static thread_t current_thread;

// CPU usage info
static cpu_info_t cpu_info;
static uint32_t task_us;
static uint32_t sleep_us;
static uint16_t loops;

static volatile bool flags;
#define FLAGS_SIGNAL        0x01
#define FLAGS_SLEEP         0x02
#define FLAGS_ACTIVE        0x04

static volatile uint16_t signals;

// KV:
static int8_t thread_i8_kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len )
{

    if( op == KV_OP_GET ){
        
        if( id == KV_ID_THREAD_COUNT ){
            
            uint8_t a = thread_u16_get_thread_count();
            memcpy( data, &a, sizeof(a) );
        }
    }

    return 0;
}

KV_SECTION_META kv_meta_t thread_info_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_THREAD_COUNT,        SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  0, thread_i8_kv_handler,       "thread_count" },
    { KV_GROUP_SYS_INFO, KV_ID_PEAK_THREAD_COUNT,   SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  &cpu_info.max_threads,     0,  "thread_peak" },
    { KV_GROUP_SYS_INFO, KV_ID_THREAD_RUN_TIME,     SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &cpu_info.run_time,        0,  "thread_run_time" },
    { KV_GROUP_SYS_INFO, KV_ID_THREAD_TASK_TIME,    SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &cpu_info.task_time,       0,  "thread_task_time" },
    { KV_GROUP_SYS_INFO, KV_ID_THREAD_SLEEP_TIME,   SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &cpu_info.sleep_time,      0,  "thread_sleep_time" },
    { KV_GROUP_SYS_INFO, KV_ID_THREAD_LOOPS,        SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &cpu_info.scheduler_loops, 0,  "thread_loops" },
};


PT_THREAD( background_thread( pt_t *pt, void *state ) );
PT_THREAD( cpu_stats_thread( pt_t *pt, void *state ) );


static uint16_t vfile( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    uint16_t ret_val = 0;

    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
                
            // iterate over data length and fill file info buffers as needed
            while( len > 0 ){
                
                uint8_t page = pos / sizeof(thread_info_t);
                
                // get thread state
                thread_t thread = list_ln_index( &thread_list, page );
                thread_state_t *state = list_vp_get_data( thread );

                // set up info page
                thread_info_t info;
                memset( &info, 0, sizeof(info) );
                
                strncpy_P( info.name, state->name, sizeof(info.name) );
                info.flags          = state->flags;
                info.thread_addr    = (uint16_t)state->thread;
                info.data_size      = list_u16_node_size( thread ) - sizeof(thread_state_t);
                info.run_time       = state->run_time;
                info.runs           = state->runs;
                info.line           = state->pt.lc;

                // get offset info page
                uint16_t offset = pos - ( page * sizeof(info) );
                
                // set copy length
                uint16_t copy_len = sizeof(info) - offset;

                if( copy_len > len ){
                    
                    copy_len = len;
                }

                // copy data
                memcpy( ptr, (void *)&info + offset, copy_len );

                // adjust pointers
                ptr += copy_len;
                len -= copy_len;
                pos += copy_len;
                ret_val += copy_len;
            }

            break;

        case FS_VFILE_OP_SIZE:
            ret_val = thread_u16_get_thread_count() * sizeof(thread_info_t);
            break;

        default:
            ret_val = 0;
            break;
    }

    return ret_val;
}


// initialize the thread scheduler
void thread_v_init( void ){

    // init thread list
    list_v_init( &thread_list );
}

// return current number of threads
uint16_t thread_u16_get_thread_count( void ){
	
	return list_u8_count( &thread_list );
}

thread_t thread_t_get_current_thread( void ){

	return current_thread;
}

void thread_v_get_cpu_info( cpu_info_t *info ){
    
    *info = cpu_info;
}

static thread_t make_thread( PT_THREAD( ( *thread )( pt_t *pt, void *state ) ),
                             PGM_P name,
                             void *initial_data,
                             uint16_t size,
                             uint8_t flags ){
    
    list_node_t ln = list_ln_create_node( 0, sizeof(thread_state_t) + size );
    
    // check if node was created
    if( ln < 0 ){
        
        return -1;
    }
    
    // get state
    thread_state_t *state = list_vp_get_data( ln );
    
    // initialize the thread context
    PT_INIT( &state->pt );
    
    state->thread   = thread;
    state->flags    = flags;
    state->name     = name;
    state->run_time = 0;
    state->runs     = 0;
    
    // copy data (if present)
    if( initial_data != 0 ){
        
        void *thread_data = state + 1;
        
        memcpy( thread_data, initial_data, size );
    }

    // check max threads
    if( thread_u16_get_thread_count() > cpu_info.max_threads ){
                
        cpu_info.max_threads = thread_u16_get_thread_count();
    }

    // add to list
    list_v_insert_tail( &thread_list, ln );

    return ln;
}

// create a thread and return its handle
thread_t thread_t_create( PT_THREAD( ( *thread )( pt_t *pt, void *state ) ),
                          PGM_P name,
                          void *initial_data,
                          uint16_t size ){
	
    return make_thread( thread, name, initial_data, size, THREAD_FLAGS_YIELDED );
}

PT_THREAD( ( *thread_p_get_function( thread_t thread_id ) ) )( pt_t *pt, void *state ){
	
    thread_state_t *state = list_vp_get_data( thread_id );

	return state->thread;
}

void *thread_vp_get_data( thread_t thread_id ){

    thread_state_t *state = list_vp_get_data( thread_id );

    return state + 1;
}

// restart a thread
void thread_v_restart( thread_t thread_id ){
	
    thread_state_t *state = list_vp_get_data( thread_id );
	
	state->flags |= THREAD_FLAGS_YIELDED;
	
	PT_INIT( &state->pt );
}

// kill a thread
void thread_v_kill( thread_t thread_id ){
    
    // remove from list
    list_v_remove( &thread_list, thread_id );

    // release node
    list_v_release_node( thread_id );
}


// active mode, set sleep to false
void thread_v_active( void ){
	
	flags |= FLAGS_ACTIVE;
	flags &= ~FLAGS_SLEEP;
}

void thread_v_signal( uint8_t signum ){
    
    ASSERT( signum < THREAD_MAX_SIGNALS );

    ATOMIC;
    
    signals |= ( (uint16_t)1 << signum );

	flags |= FLAGS_SIGNAL;
	flags &= ~FLAGS_SLEEP;

    END_ATOMIC;
}

void thread_v_clear_signal( uint8_t signum ){
    
    ASSERT( signum < THREAD_MAX_SIGNALS );

    ATOMIC;
    
    signals &= ~( (uint16_t)1 << signum );

    END_ATOMIC;
}

bool thread_b_signalled( uint8_t signum ){
    
    ASSERT( signum < THREAD_MAX_SIGNALS );

    uint16_t sig_copy;

    ATOMIC;
    
    sig_copy = signals;

    END_ATOMIC;

    return ( sig_copy & ( (uint16_t)1 << signum ) ) != 0;
}

void thread_v_set_signal_flag( void ){
    
    thread_state_t *state = list_vp_get_data( thread_t_get_current_thread() );
	
	state->flags |= THREAD_FLAGS_SIGNAL;
}

void thread_v_clear_signal_flag( void ){

    thread_state_t *state = list_vp_get_data( thread_t_get_current_thread() );
	
	state->flags &= ~THREAD_FLAGS_SIGNAL;
}

void run_thread( thread_t thread, thread_state_t *state ){
    
    uint32_t thread_ticks = tmr_u32_get_ticks();
	
	// set current thread
	current_thread = thread;
	
    // clear active flag
    flags &= ~FLAGS_ACTIVE;

    // get pointer to state memory
    void *ptr = state + 1;
    
    // stack check
    // this is in case a thread overflows the stack.
    // this variable will (probably) get stomped on first,
    volatile uint32_t stack_check = 0x12345678;

    // run the thread
    char status = state->thread( &state->pt, ptr );
    
    // .... and then we'll assert after the thread
    // smashes the stack.
    ASSERT( stack_check == 0x12345678 );

    // compute run time
    if( flags & FLAGS_ACTIVE ){
        
        uint32_t last_run_time = state->run_time;
        uint32_t elapsed_us = tmr_u32_ticks_to_us( tmr_u32_elapsed_ticks( thread_ticks ) );
        task_us += elapsed_us;
        state->run_time += elapsed_us;

        // check for overflow
        if( state->run_time < last_run_time ){

            state->run_time = 0xffffffff;
        }

        // check for overflow
        if( state->runs < 0xffffffff ){

            state->runs++;
        }
    }

    // check returned thread state
    switch( status ){
        
        // thread is waiting.
        // generally, this is used if the thread is waiting on
        // some resource which is indirectly tied to hardware.
        // as such, we won't increment the ready threads count
        // for this case, so the processor can eventually go to
        // sleep.
        // only use this when its ok if the processor goes to 
        // sleep during the wait.  If it is imperative to keep
        // the processor awake, use the yield instead.
        case PT_WAITING:

            state->flags |= THREAD_FLAGS_WAITING;
            
            break;
        
        // thread yielded, it has more processing to do
        case PT_YIELDED:
            
            state->flags |= THREAD_FLAGS_YIELDED;
            
            break;
        
        // thread has gone to sleep
        case PT_SLEEPING:
            
            state->flags |= THREAD_FLAGS_SLEEPING;
            
            break;
        
        // if the thread has completed, 
        case PT_EXITED:
        case PT_ENDED:
            
            // remove the thread
            thread_v_kill( current_thread );
            
            break;
        
        default:
            
            ASSERT( FALSE );
            break;
    }
    
    #ifdef ENABLE_THREAD_DISABLE_INTERRUPTS_CHECK
    // check if the global interrupts are still enabled
    ASSERT_MSG( ( SREG & 0x80 ) != 0, "Global interrupts disabled!" );
    #endif
}

void process_signalled_threads( void ){
	
    // clear signal flag
    flags &= ~FLAGS_SIGNAL;
    
    // iterate through thread list
    list_node_t ln = thread_list.head;
    
    while( ln >= 0 ){
        
        list_node_state_t *ln_state = mem2_vp_get_ptr_fast( ln );
        thread_state_t *state = (thread_state_t *)&ln_state->data;

        if( ( state->flags & THREAD_FLAGS_SIGNAL ) != 0 ){
            
            // clear wait flags
            state->flags &= ~THREAD_FLAGS_WAITING;
            state->flags &= ~THREAD_FLAGS_YIELDED;

            run_thread( ln, state );
        }

        ln = ln_state->next;
    }
}	

// start the thread scheduler
void thread_start( void ){
	
	// start the background threads
	thread_t_create( background_thread, PSTR("background"), 0, 0 );
    thread_t_create( cpu_stats_thread, PSTR("cpu_stats"), 0, 0 );

    // create vfile
    fs_f_create_virtual( PSTR("threadinfo"), vfile );


    static uint32_t ticks;
    
	// infinite loop running the thread scheduler
	while(1){
		
        ticks = tmr_u32_get_ticks();
        
		// set sleep flag
		flags |= FLAGS_SLEEP;
		
		// ********************************************************************
		// Process Waiting threads
		//
		// Loop through all waiting threads
		// ********************************************************************
        
        list_node_t ln = thread_list.head;
        
        while( ln >= 0 ){
        
            if( flags & FLAGS_SIGNAL ){
                
                process_signalled_threads();
            }

            sys_v_wdt_reset();
			
            list_node_state_t *ln_state = mem2_vp_get_ptr_fast( ln );
            
            thread_state_t *state = (thread_state_t *)&ln_state->data;
				
			if( ( ( ( state->flags & THREAD_FLAGS_WAITING ) != 0 ) ||
				  ( ( state->flags & THREAD_FLAGS_YIELDED ) != 0 ) ) &&
				( ( state->flags & THREAD_FLAGS_SIGNAL ) == 0 ) ){
				
				// clear wait flags
				state->flags &= ~THREAD_FLAGS_WAITING;
				state->flags &= ~THREAD_FLAGS_YIELDED;
				
				// run the thread
				run_thread( ln, state );
			}

            ln = ln_state->next;
		}
		
		// ********************************************************************
		// Check for sleep conditions
		//
		// If no IRQ threads, and all threads are asleep, enter sleep mode
		// ********************************************************************
		if( flags & FLAGS_SLEEP ){
			
            ticks = tmr_u32_get_ticks();
            
			sys_v_sleep( SLP_IDLE );
            // zzzzzzzzzzzzz

            sleep_us += tmr_u32_ticks_to_us( tmr_u32_elapsed_ticks( ticks ) );
		}
		
        #ifdef __SIM__
        break;
        #endif

        if( loops < 0xffff ){
            
            loops++;
        }
	}
}

PT_THREAD( background_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );
	
	static uint32_t timer;
    
	while(1){
		
        timer = 100;
        TMR_WAIT( pt, timer );
        
        sys_v_wdt_reset();
	}
	
PT_END( pt );
}


PT_THREAD( cpu_stats_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );
    
    static uint32_t timer;
    static uint32_t timestamp;
    
    while(1){
        
        // mark current timestamp
        timestamp = tmr_u32_get_system_time_ms();

        // reset counters
        loops = 0;
        task_us = 0;
        sleep_us = 0;

        timer = 5000;
        TMR_WAIT( pt, timer );

        cpu_info.run_time = tmr_u32_elapsed_time( timestamp );
        cpu_info.task_time = task_us / 1000;
        cpu_info.sleep_time = sleep_us / 1000;
        cpu_info.scheduler_loops = loops;
    }
    
PT_END( pt );
}



