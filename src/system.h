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

#ifndef _SYSTEM_H
#define _SYSTEM_H

#include "cpu.h"
#include "boot_data.h"
#include "target.h"				

//#define ENABLE_MEMORY_DUMP

// no user selectable options exist below this line!

#define FW_INFO_ADDRESS 0x120 // this must match the offset in the makefile!
#define FW_ID_LENGTH 16

typedef struct{
    uint32_t fw_length;
    uint8_t fwid[FW_ID_LENGTH];
    char os_name[128];
    char os_version[16];
    char app_name[128];
    char app_version[16];
} fw_info_t;

typedef struct{
	uint64_t serial_number;
	uint64_t mac;
	uint16_t model;
	uint16_t rev;
} hw_info_t;

typedef uint8_t sys_mode_t8;
#define SYS_MODE_NORMAL     0
#define SYS_MODE_SAFE       1
#define SYS_MODE_NO_APP     2
#define SYS_MODE_FORMAT     3


typedef uint8_t sys_error_t;
#define SYS_ERR_NONE                    0
#define SYS_ERR_INVALID_HANDLE          1
#define SYS_ERR_HANDLE_UNALLOCATED      2
#define SYS_ERR_INVALID_CANARY          3
#define SYS_ERR_MEM_BLOCK_IS_DIRTY      4

typedef uint32_t sys_warnings_t;
#define SYS_WARN_MEM_FULL               0x0001
#define SYS_WARN_NETMSG_FULL            0x0002
#define SYS_WARN_FLASHFS_FAIL           0x0004
#define SYS_WARN_FLASHFS_HARD_ERROR     0x0008
#define SYS_WARN_CONFIG_FULL            0x0010
#define SYS_WARN_CONFIG_WRITE_FAIL      0x0020


typedef uint8_t bool;
#define TRUE 1
#define FALSE 0

// Critical Section
//
// Notes:  ATOMIC creates a local copy of SREG, but only copies the I bit.
// END_ATOMIC restores the original value of the SREG I bit, but does not modify any other bits.
// This guarantees interrupts will be disabled within the critical section, but does not
// enable interrupts if they were already disabled.  Since it also doesn't modify any of the 
// other SREG bits, it will not disturb instructions after the critical section.
// An example of this would be performing a comparison inside the critical section, and then
// performing a branch immediately thereafter.  If we restored SREG in its entirety, we will
// have destroyed the result of the compare and the code will not execute as intended.
#ifndef __SIM__
    #define ATOMIC uint8_t __sreg_i = ( SREG & 0b10000000 ); cli()
    #define END_ATOMIC SREG |= __sreg_i
#else
    #define ATOMIC
    #define END_ATOMIC
#endif

//#undef INCLUDE_COMPILER_ASSERTS
//#undef INCLUDE_ASSERTS
//#define INCLUDE_ASSERT_TEXT

// This setting will allow the system to disable assertion traps at runtime.  Do not enable this option unless there is an extremely good reason for it.
//#define ALLOW_ASSERT_DISABLE

// define compile assert macro
// if the expression evaluates to false
#ifdef INCLUDE_COMPILER_ASSERTS
	#define COMPILER_ASSERT(expression) switch(0) { case 0 : case (expression) : ; }
#else
	#define COMPILER_ASSERT(expression)   
#endif	

#define FLASH_STRING(x) PSTR(x)
#define FLASH_STRING_T  PGM_P

#ifdef INCLUDE_ASSERTS
	#ifdef INCLUDE_ASSERT_TEXT
	
		#define ASSERT(expr)  if( !(expr) ){  assert( FLASH_STRING( #expr ), FLASH_STRING( __FILE__ ), __LINE__); }
		#define ASSERT_MSG(expr, str) if( !(expr) ){ assert( FLASH_STRING( #str ), FLASH_STRING( __FILE__ ), __LINE__); }
	#else
	
		#define ASSERT(expr)  if( !(expr) ){  assert( 0, FLASH_STRING( __FILE__ ), __LINE__); }
		#define ASSERT_MSG(expr, str) ASSERT(expr)
	#endif
	void assert(FLASH_STRING_T str_expr, FLASH_STRING_T file, int line);
#else
	#define ASSERT(expr)
	#define ASSERT_MSG(expr, str)
#endif


// Count of array macro
#define cnt_of_array( array ) ( sizeof( array ) / sizeof( array[0] ) )


typedef uint8_t sys_sleep_mode_t8;
enum{
	SLP_ACTIVE,
	SLP_IDLE,
	SLP_ADCNRM,
	SLP_PWRDN,
	SLP_PWRSAVE,
	SLP_STNDBY,
	SLP_EXSTNDBY,
};

typedef uint8_t sys_clock_t8;
#define	CLK_DIV_1	0b00000000
#define	CLK_DIV_2 	0b00000001
#define CLK_DIV_4 	0b00000010
#define	CLK_DIV_8 	0b00000011
#define	CLK_DIV_16 	0b00000100
#define	CLK_DIV_32  0b00000101
#define	CLK_DIV_64	0b00000110
#define	CLK_DIV_128 0b00000111
#define	CLK_DIV_256 0b00001000


void sys_v_init( void );
void sys_v_check_io_for_safe_mode( void );

sys_mode_t8 sys_u8_get_mode( void );
loader_status_t8 sys_u8_get_loader_status( void );

void sys_v_get_fw_id( uint8_t id[FW_ID_LENGTH] );
void sys_v_get_fw_info( fw_info_t *fw_info );
void sys_v_get_hw_info( hw_info_t *hw_info );

void sys_v_sleep( sys_sleep_mode_t8 mode );

void sys_reboot( void ) __attribute__((noreturn));
void sys_reboot_to_loader( void ) __attribute__((noreturn));
void sys_v_load_fw( void );
void sys_v_reboot_delay( sys_mode_t8 mode );

void sys_v_wdt_reset( void );
void sys_v_init_watchdog( void );

void sys_v_get_boot_data( boot_data_t *data );
boot_mode_t8 sys_m_get_boot_mode( void );

#ifdef ALLOW_ASSERT_DISABLE
void sys_v_enable_assertion_trap( void );
void sys_v_disable_assertion_trap( void );
void sys_v_reset_assertion_trap( void );
bool sys_v_asserted( void );
#endif

void sys_v_set_error( sys_error_t error );

void sys_v_set_warnings( sys_warnings_t flags );
sys_warnings_t sys_u32_get_warnings( void );

void sys_v_set_clock_prescaler( sys_clock_t8 prescaler );

#endif


