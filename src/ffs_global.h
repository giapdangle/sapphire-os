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

#ifndef _FFS_GLOBAL_H
#define _FFS_GLOBAL_H

#include "flash_fs_partitions.h"

#define FFS_IO_ATTEMPTS             3

#define FLASH_FS_MAX_FILES			17
#define FFS_MAX_FILES			    ( FLASH_FS_MAX_FILES - 1 ) 
// the -1 accounts for the firmware partition

#define FFS_FILENAME_LEN		    64

#define FFS_FILE_ID_FIRMWARE        ( FFS_MAX_FILES )


#define FFS_WEAR_THRESHOLD          1024

typedef int8_t ffs_file_t;


#define FFS_PAGE_DATA_SIZE          64
#define FFS_DATA_PAGES_PER_BLOCK    51
#define FFS_SPARE_PAGES_PER_BLOCK   8
#define FFS_PAGES_PER_BLOCK         ( FFS_DATA_PAGES_PER_BLOCK + FFS_SPARE_PAGES_PER_BLOCK )
//#define FFS_TOTAL_PAGES             ( FFS_PAGES_PER_BLOCK * FLASH_FS_FILE_SYSTEM_N_BLOCKS )
#define FFS_BLOCK_DATA_SIZE         ( FFS_DATA_PAGES_PER_BLOCK * FFS_PAGE_DATA_SIZE )

// function status codes
#define FFS_STATUS_OK   			0
#define FFS_STATUS_BUSY 			-1
#define FFS_STATUS_NO_FREE_SPACE 	-2
#define FFS_STATUS_NO_FREE_FILES 	-3
#define FFS_STATUS_INVALID_FILE 	-4
#define FFS_STATUS_EOF			 	-5
#define FFS_STATUS_ERROR		 	-6
#define FFS_STATUS_PAGE_NOT_ERASED  -7


#endif

