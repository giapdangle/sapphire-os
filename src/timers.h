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



#ifndef _TIMERS_H
#define _TIMERS_H

#include "system.h"
#include "threading.h"

// milliseconds per timer tick (for keeping system time)
#define TIMER_MS_PER_TICK 10

// number of timer ticks per ms
#define TICKS_PER_MS 2000

#define TIMER_TOP   ( TICKS_PER_MS * 10 )


// function prototypes:
void tmr_v_init( void );
uint32_t tmr_u32_get_system_time_ms( void );
uint32_t tmr_u32_get_system_time_us( void );
uint32_t tmr_u32_get_system_time_irq( void );
uint32_t tmr_u32_get_system_time( void );
uint32_t tmr_u32_elapsed_time( uint32_t start_time );
int8_t tmr_i8_compare_time( uint32_t time );
int8_t tmr_i8_compare_times( uint32_t time1, uint32_t time2 );
uint32_t tmr_u32_get_ticks( void );
uint32_t tmr_u32_elapsed_ticks( uint32_t start_ticks );
uint32_t tmr_u32_ticks_to_us( uint32_t ticks );

#define TMR_WAIT( pt, time ) \
	time += tmr_u32_get_system_time(); \
	THREAD_WAIT_WHILE( pt, tmr_i8_compare_time( time ) > 0 )

#endif


