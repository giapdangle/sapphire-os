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

#include "boot_data.h"
#include "system.h"
#include "config.h"
#include "threading.h"
#include "timers.h"
#include "target.h"
#include "datetime.h"
#include "io.h"
#include "wcom_neighbors.h"
#include "keyvalue.h"
#include "flash25.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define NO_LOGGING
#include "logging.h"



typedef struct{
	uint32_t timer;
} reboot_thread_state_t;


#ifdef ALLOW_ASSERT_DISABLE
static bool disable_assertions;
static bool asserted;
#endif

static sys_mode_t8 sys_mode;

static sys_error_t sys_error;
static sys_warnings_t warnings;

// bootloader shared memory
boot_data_t BOOTDATA boot_data;

// local functions:
void reboot( void ) __attribute__((noreturn));
void init_watchdog( void );


PT_THREAD( sys_reboot_thread( pt_t *pt, reboot_thread_state_t *state ) );


KV_SECTION_META kv_meta_t sys_info_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_SYS_MODE,                SAPPHIRE_TYPE_UINT8,  KV_FLAGS_READ_ONLY,  &sys_mode,                       0,  "sys_mode" },
    { KV_GROUP_SYS_INFO, KV_ID_BOOT_MODE,               SAPPHIRE_TYPE_UINT8,  KV_FLAGS_READ_ONLY,  &boot_data.boot_mode,            0,  "boot_mode" },
    { KV_GROUP_SYS_INFO, KV_ID_LOADER_VERSION_MINOR,    SAPPHIRE_TYPE_UINT8,  KV_FLAGS_READ_ONLY,  &boot_data.loader_version_minor, 0,  "loader_version_minor" },
    { KV_GROUP_SYS_INFO, KV_ID_LOADER_VERSION_MAJOR,    SAPPHIRE_TYPE_UINT8,  KV_FLAGS_READ_ONLY,  &boot_data.loader_version_major, 0,  "loader_version_major" },
    { KV_GROUP_SYS_INFO, KV_ID_LOADER_STATUS,           SAPPHIRE_TYPE_UINT8,  KV_FLAGS_READ_ONLY,  &boot_data.loader_status,        0,  "loader_status" },
    { KV_GROUP_SYS_INFO, KV_ID_SYS_WARNINGS,            SAPPHIRE_TYPE_UINT32, KV_FLAGS_READ_ONLY,  &warnings,                       0,  "sys_warnings" },
};

void sys_v_init( void ){

	// check that boot_data is the right size
	COMPILER_ASSERT( sizeof( boot_data ) <= 8 );
	
	// increment reboot counter
	boot_data.reboots++;

    // initialize loader command
    boot_data.loader_command = LDR_CMD_NONE;

    #ifndef __SIM__
    
	// check reset source
	uint8_t reset_source = MCUSR;
	
	// power on reset (or JTAG)
	if( ( reset_source & ( 1 << PORF ) ) || ( reset_source & ( 1 << JTRF ) ) || ( reset_source & ( 1 << BORF ) ) ){

        sys_mode = SYS_MODE_NORMAL;
		boot_data.reboots = 0; // initialize reboot counter
    }
    // check if format requested
    else if( boot_data.boot_mode == BOOT_MODE_FORMAT ){
        
        sys_mode = SYS_MODE_FORMAT;
    }
	// check if there was reset not caused by a commanded reboot
	else if( boot_data.boot_mode != BOOT_MODE_REBOOT ){
          
        sys_mode = SYS_MODE_SAFE;
	}
    else{
        
       sys_mode = SYS_MODE_NORMAL;
    }

    // set boot mode to normal
    boot_data.boot_mode = BOOT_MODE_NORMAL;

	// clear MCU status register (clears reset source)
	MCUSR = 0;
	
	// move ISRs to app
	MCUCR |= _BV( IVCE );
	
	uint8_t mcucr = MCUCR;			
	
	mcucr &= ~_BV( IVCE );
	mcucr &= ~_BV( IVSEL );
		
	MCUCR = mcucr;	
	#endif
}

void sys_v_check_io_for_safe_mode( void ){
    
    if( io_b_button_down() ){
        
        sys_mode = SYS_MODE_SAFE;
    }
}

sys_mode_t8 sys_u8_get_mode( void ){
    
    return sys_mode;
}

loader_status_t8 sys_u8_get_loader_status( void ){
    
    return boot_data.loader_status;
}

void sys_v_get_boot_data( boot_data_t *data ){
	
	*data = boot_data;
}

boot_mode_t8 sys_m_get_boot_mode( void ){
	
	return boot_data.boot_mode;
}

#ifdef ALLOW_ASSERT_DISABLE
void sys_v_enable_assertion_trap( void ){

    disable_assertions = FALSE;
}

void sys_v_disable_assertion_trap( void ){
    
    disable_assertions = TRUE;
}

void sys_v_reset_assertion_trap( void ){
    
    asserted = FALSE;
}

bool sys_v_asserted( void ){
    
    return asserted;
}
#endif

void sys_v_set_error( sys_error_t error ){
    
    sys_error = error;
}

void sys_v_set_warnings( sys_warnings_t flags ){
    
    if( !( warnings & flags ) ){
        
        if( flags & SYS_WARN_MEM_FULL ){
            
            log_v_warn_P( PSTR("Mem full") );
        }
        
        if( flags & SYS_WARN_NETMSG_FULL ){
            
            log_v_warn_P( PSTR("Netmsg full") );
        }

        if( flags & SYS_WARN_CONFIG_FULL ){
            
            log_v_warn_P( PSTR("Config full") );
        }

        if( flags & SYS_WARN_CONFIG_WRITE_FAIL ){
            
            log_v_warn_P( PSTR("Config write fail") );
        }
    }

    warnings |= flags;
}

sys_warnings_t sys_u32_get_warnings( void ){

    return warnings;
}

// set mcu sleep mode and enter sleep
void sys_v_sleep( sys_sleep_mode_t8 mode ){
	
	uint8_t sleep_bits = 0;
	
	// disable interrupts
	ATOMIC;
	
	switch( mode ){
		case SLP_ACTIVE:
			sleep_bits = 0b00000000;
			break;
	
		case SLP_IDLE:
			sleep_bits = 0b00000001;
			break;
			
		case SLP_ADCNRM:
			sleep_bits = 0b00000011;
			break;
			
		case SLP_PWRDN:
			sleep_bits = 0b00000101;
			break;
		
		case SLP_PWRSAVE:
			sleep_bits = 0b00000111;
			break;
		
		case SLP_STNDBY:
			sleep_bits = 0b00001101;
			break;
		
		case SLP_EXSTNDBY:
			sleep_bits = 0b00001111;
			break;
		
		default:
			ASSERT_MSG( FALSE, "Invalid sleep mode" );
			break;
	}
	
    #ifndef __SIM__
	// set sleep mode
	SMCR = sleep_bits;
	
	// re-enable interrupts
	END_ATOMIC;
	
	// enter sleep mode
	sleep_mode();
	
	
	// sleeping....  zzzzzz
	
	// clear sleep mode to prevent inadvertently entering sleep
	SMCR = 0;
    #endif
}

void sys_v_get_fw_id( uint8_t id[FW_ID_LENGTH] ){
    
    memcpy_P( id, (void *)FW_INFO_ADDRESS + offsetof(fw_info_t, fwid), FW_ID_LENGTH );
}

void sys_v_get_fw_info( fw_info_t *fw_info ){
    
    memcpy_P( fw_info, (void *)FW_INFO_ADDRESS, sizeof(fw_info_t) );
}

void sys_v_get_hw_info( hw_info_t *hw_info ){

    flash25_v_read( 0, hw_info, sizeof(hw_info_t) );
}

// causes a watchdog timeout, which will cause a reset into the bootloader.
// this will request an immediate reboot from the loader.
void sys_reboot( void ){
    
    boot_data.boot_mode = BOOT_MODE_REBOOT;

	reboot();
}

// immediate reset into bootloader
void sys_reboot_to_loader( void ){
    
    boot_data.loader_command = LDR_CMD_SERIAL_BOOT;
    boot_data.boot_mode = BOOT_MODE_REBOOT;

	reboot();
}

// reboot with a load firmware command to the bootloader
void sys_v_load_fw( void ){
    
    boot_data.loader_command = LDR_CMD_LOAD_FW;

    sys_v_reboot_delay( SYS_MODE_NORMAL );
}

// start reboot delay thread
void sys_v_reboot_delay( sys_mode_t8 mode ){
	
    if( mode == SYS_MODE_SAFE ){
        
        // this looks weird.
        // the system module will check the boot mode when it initializes.
        // if it came up from a normal reboot, it expects to see reboot set.
        // if normal mode, it assumes an unexpected reboot, which causes it
        // to go in to safe mode.
        boot_data.boot_mode = BOOT_MODE_NORMAL;
    }
    else if( mode == SYS_MODE_FORMAT ){
        
        boot_data.boot_mode = BOOT_MODE_FORMAT;
    }
    else{
        
        boot_data.boot_mode = BOOT_MODE_REBOOT;
	}

    thread_t_create( THREAD_CAST(sys_reboot_thread), 
                     PSTR("reboot"),
                     0, 
                     sizeof( reboot_thread_state_t ) );
}

void reboot( void ){
	
	// make sure interrupts are disabled
	cli();

	// make sure the watchdog is turned on
	sys_v_init_watchdog();
	
	// infinite loop, wait for reset
	for(;;);
}

// runtime assertion handling.
// note all assertions are considered fatal, and will result in the system
// rebooting into the bootloader.  it will pass the assertion information
// to the bootloader so it can report the error.
#ifdef INCLUDE_ASSERTS
void assert(FLASH_STRING_T str_expr, FLASH_STRING_T file, int line){
	
    #ifdef ALLOW_ASSERT_DISABLE
    if( disable_assertions ){
        
        asserted = TRUE;

        return;
    }
    #endif

	// disable interrupts, as this is a fatal error
	cli();
	
    // create error log
    cfg_error_log_t error_log;
    memset( &error_log, 0, sizeof(error_log) );
    
    // get pointer
    void *ptr = error_log.log;
    
    // copy flash file name pointer to memory variable
    char filename[32];
    strncpy_P( filename, file, sizeof(filename) );
    
    // get thread function pointer
    uint32_t thread_addr = (uint32_t)((uint16_t)thread_p_get_function( thread_t_get_current_thread() ));
    // the double casts are to prevent a compiler warning from the pointer casting to a larger integer.
    
    // multiple the address by 2 to get the byte address.
    // this makes it easy to look up the thread function in the .lss file.
    thread_addr <<= 1;
    
    // write assert data
    ptr += sprintf_P( ptr, PSTR("Assert File: %s Line: %d Thread: 0x%x \r\n"), filename, line, thread_addr );
    
    // write sys error code
    ptr += sprintf_P( ptr, PSTR("Error: %d \r\n"), sys_error );
    
    // write system time
    ptr += sprintf_P( ptr, PSTR("System Time: %ld \r\n"), tmr_u32_get_system_time_ms() );
    
    // write network time
    datetime_t datetime;
    datetime_v_now( &datetime );
    char buf[ISO8601_STRING_MIN_LEN];
    datetime_v_to_iso8601( buf, sizeof(buf), &datetime );
    ptr += sprintf_P( ptr, PSTR("SNTP Time: %s \r\n"), buf );
    
    // write memory data
    mem_rt_data_t rt_data;
    mem2_v_get_rt_data( &rt_data );
    
    ptr += sprintf_P(   ptr, 
                        PSTR("Memory: Handles: %d Free: %d Used: %d Dirty: %d Data: %d \r\n"), 
                        rt_data.handles_used,
                        rt_data.free_space,
                        rt_data.used_space,
                        rt_data.dirty_space,
                        rt_data.data_space );    
    
    #ifdef ENABLE_MEMORY_DUMP
    // dump memory
    // this only works with a small number of handles.
    // try compiling with 32 or less.
    for( uint16_t i = 0; i < MAX_MEM_HANDLES; i++ ){

        mem_block_header_t header = mem2_h_get_header( i );
        
        ptr += sprintf_P( ptr, 
                          PSTR("%3d:%4x\r\n"), 
                          header.size,
                          #ifdef ENABLE_RECORD_CREATOR
                          header.creator_address
                          #else
                          0
                          #endif
                          );
    }
    #endif
    
    // write error log
    cfg_v_write_error_log( &error_log );
    
    // reboot
    reboot();
}
#endif

void sys_v_wdt_reset( void ){
	wdt_reset();
}

void sys_v_init_watchdog( void ){

    #ifndef __SIM__
	// enable watchdog timer:
	wdt_reset();
	WDTCSR |= ( 1 << WDCE ) | ( 1 << WDE );
	//WDTCSR = ( 1 << WDE) | 0b00000100; // watchdog timeout at 256 ms, reset only
	WDTCSR = ( 1 << WDE) | 0b00000111; // watchdog timeout at 2048 ms, reset only
    #endif
}

#ifndef __SIM__
ISR( WDT_vect ){
	
}

// bad interrupt
ISR(__vector_default){
	
	ASSERT( 0 );
}
#endif



// set the system clock prescaler.
// this function disable interrupts and does NOT re-enable them!
void sys_v_set_clock_prescaler( sys_clock_t8 prescaler ){
	
	// disable interrupts
	cli();
    
	#ifndef __SIM__
	CLKPR = 0b10000000; // enable the prescaler change
	CLKPR = prescaler; // set the prescaler
    #endif
	
}


PT_THREAD( sys_reboot_thread( pt_t *pt, reboot_thread_state_t *state ) )
{
PT_BEGIN( pt );  

    // notify boot mode
    kv_i8_notify( KV_GROUP_SYS_INFO, KV_ID_BOOT_MODE );

	state->timer = 1000;
	
	TMR_WAIT( pt, state->timer );

    // flush neighbors
    wcom_neighbors_v_flush();

	state->timer = 100;
	
	TMR_WAIT( pt, state->timer );
	
	reboot();
	
PT_END( pt );
}


