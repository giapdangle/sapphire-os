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

#include "threading.h"
#include "target.h"

#include "timers.h"

#ifdef __SIM__
uint32_t system_time;
#else
static volatile uint32_t system_time; // current time in milliseconds
#endif

void init_timer_1( void );


static int8_t tmr_i8_kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len )
{

    if( op == KV_OP_GET ){
        
        if( id == KV_ID_SYS_TIME ){

            uint32_t time = tmr_u32_get_system_time_ms();
            
            memcpy( data, &time, sizeof(time) );
        }
    }

    return 0;
}


KV_SECTION_META kv_meta_t tmr_info_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_SYS_TIME, SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  0, tmr_i8_kv_handler,  "sys_time" },
};

void tmr_v_init( void ){
	
	system_time = 0;
	
	init_timer_1();
}


// returns the current system time, accurate to the nearest millisecond.
// this uses the current system time and the current timer count
uint32_t tmr_u32_get_system_time_ms( void ){
	
    #ifdef __SIM__
    uint32_t time = system_time;
    #else
	ATOMIC;
	uint32_t time = system_time + ( TCNT1 / TICKS_PER_MS );
	END_ATOMIC;
    #endif
    
	return time;
}

// returns the current system time, accurate to the nearest microsecond.
// this uses the current system time and the current timer count
uint32_t tmr_u32_get_system_time_us( void ){
	
    #ifdef __SIM__
    uint32_t time = system_time;
    #else
	ATOMIC;
	uint32_t time = ( system_time * 1000 ) + ( TCNT1 / ( TICKS_PER_MS / 1000 ) );
	END_ATOMIC;
    #endif
    
	return time;
}

// returns the current value of the system timer.
// this is the count driven by the timer interrupt.
// the count is in milliseconds, but is only accurate to the interrupt
// rate, not necessarily to the nearest millisecond.
uint32_t tmr_u32_get_system_time( void ){

	// protect access to system time
	ATOMIC;
	uint32_t time = system_time;
	END_ATOMIC;
	
	return time;
}

// version for interrupt routines, does not re-enable interrupts.
// this will still disable interrupts, so if called from a non-ISR, this
// will effectively halt the processor (so you'll know you screwed up)
uint32_t tmr_u32_get_system_time_irq( void ){
	
	cli();
	uint32_t time = system_time;
	
	return time;
}

// compute elapsed ticks since start_ticks
uint32_t tmr_u32_elapsed_time( uint32_t start_time ){
	
	uint32_t end_time = tmr_u32_get_system_time_ms();
	
	if( end_time >= start_time ){
		
		return end_time - start_time;
	}
	else{
		
		return UINT32_MAX - ( start_time - end_time );
	}
}

// returns 1 if time > system_time,
// -1 if time < system_time,
// 0 if equal
int8_t tmr_i8_compare_time( uint32_t time ){
	
	ATOMIC;
	int32_t distance = ( int32_t )( time - system_time );
	END_ATOMIC;
	
	if( distance < 0 ){
	
		return -1;
	}
	else if( distance > 0 ){
	
		return 1;
	}
	else{
	
		return 0;
	}
}

// returns 1 if time1 > time2,
// -1 if time1 < time2,
// 0 if equal
int8_t tmr_i8_compare_times( uint32_t time1, uint32_t time2 ){
		
	int32_t distance = ( int32_t )( time1 - time2 );
	
	if( distance < 0 ){
	
		return -1;
	}
	else if( distance > 0 ){
	
		return 1;
	}
	else{
	
		return 0;
	}
}

uint32_t tmr_u32_get_ticks( void ){
	
	ATOMIC;
	
    #ifdef __SIM__
    uint16_t current_timer = 0;
    #else
	uint16_t current_timer = TCNT1;
	#endif
    
	uint32_t ticks = system_time;
	
	END_ATOMIC;
	
	ticks *= (uint32_t)TICKS_PER_MS; // top = 4,294,967,000
	ticks += current_timer;
	
	// max 4,294,967,295 - 4,294,967,000 = 295
	
	return ticks;
}

// compute elapsed ticks since start_ticks
uint32_t tmr_u32_elapsed_ticks( uint32_t start_ticks ){
	
	uint32_t end_ticks;
	end_ticks = tmr_u32_get_ticks();
	
	if( end_ticks >= start_ticks ){
		
		return end_ticks - start_ticks;
	}
	else{
		
		return UINT32_MAX - ( start_ticks - end_ticks );
	}
}

uint32_t tmr_u32_ticks_to_us( uint32_t ticks ){
    
    return ticks / 2;
}
	
void init_timer_1( void ){

	/*
	TODO:
	
	Modify this function to check the system clock prescaler from the system
	module, and set the timer compare match accordingly.  This makes the timer
	module independent of system clock settings.
	
	*/

    #ifndef __SIM__
	// compare match timing:
	// 16,000,000 / 8 = 2,000,000
	// 2,000,000 / 2000 = 1000 hz.
	OCR1A = TIMER_TOP - 1;
	
	TCCR1A = 0b00000000;
	TCCR1B = 0b00000010; // prescaler / 8
	TCCR1C = 0;
	
	TIMSK1 = 0b00000010; // compare match A interrupt enabled
    #endif
}

#ifndef __SIM__
// Timer 1 compare match interrupt:
ISR(TIMER1_COMPA_vect){
	
    while( TCNT1 > TIMER_TOP ){

        // increment system time
        system_time += 10;
        
        // subtract timer max
        TCNT1 -= TIMER_TOP;
    }
}
#endif
