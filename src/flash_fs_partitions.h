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

#ifndef _FLASH_FS_PARTITIONS_H
#define _FLASH_FS_PARTITIONS_H

#include "system.h"

#define FLASH_FS_N_ERASE_BLOCKS			128
#define FLASH_FS_ERASE_BLOCK_SIZE		4096

// array size is the size of the entire flash memory array
#define FLASH_FS_ARRAY_SIZE				( (uint32_t)FLASH_FS_N_ERASE_BLOCKS * (uint32_t)FLASH_FS_ERASE_BLOCK_SIZE )

// Partitions:
// Firmware partition
#define FLASH_FS_FIRMWARE_0_PARTITION_START	( FLASH_FS_ERASE_BLOCK_SIZE ) // start at block 1
#define FLASH_FS_FIRMWARE_0_PARTITION_SIZE	( (uint32_t)128 * (uint32_t)1024 ) // in bytes
#define FLASH_FS_FIRMWARE_0_N_BLOCKS	    ( FLASH_FS_FIRMWARE_0_PARTITION_SIZE / FLASH_FS_ERASE_BLOCK_SIZE ) // in blocks

// User file system partition 1:
// file system size is the size of the file system partition
#define FLASH_FS_FILE_SYSTEM_START		( FLASH_FS_FIRMWARE_0_PARTITION_START + FLASH_FS_FIRMWARE_0_PARTITION_SIZE )
#define FLASH_FS_FILE_SYSTEM_SIZE		( FLASH_FS_ARRAY_SIZE - FLASH_FS_FILE_SYSTEM_START )
#define FLASH_FS_FILE_SYSTEM_N_BLOCKS	( FLASH_FS_FILE_SYSTEM_SIZE / FLASH_FS_ERASE_BLOCK_SIZE )

#define FLASH_FS_N_PARTITIONS 2


#endif

























