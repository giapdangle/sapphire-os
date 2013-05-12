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

#ifndef _BOOT_CONSTS_H
#define _BOOT_CONSTS_H

#include "cpu.h"

// define bootloader data transfer section
// there are 8 bytes available in the bootdata section
#define BOOTDATA __attribute ((section (".noinit"))) 

typedef uint8_t boot_mode_t8;
#define BOOT_MODE_NORMAL    0
#define BOOT_MODE_REBOOT    1
#define BOOT_MODE_FORMAT    2

typedef uint16_t loader_command_t16;
#define LDR_CMD_NONE        0x0000
#define LDR_CMD_LOAD_FW     0x2012
#define LDR_CMD_SERIAL_BOOT 0x3023

typedef uint8_t loader_status_t8;
#define LDR_STATUS_NORMAL           0
#define LDR_STATUS_NEW_FW           1
#define LDR_STATUS_RECOVERED_FW     2

typedef struct{
	uint16_t reboots;
	boot_mode_t8 boot_mode;
    loader_command_t16 loader_command;	
    uint8_t loader_version_major;
    uint8_t loader_version_minor;
    loader_status_t8 loader_status;
} boot_data_t;


#endif
