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

#ifndef _FFS_PAGE_H
#define _FFS_PAGE_H

#include "ffs_global.h"
#include "ffs_block.h"

#define FFS_PAGE_INDEX_FREE     0xff

typedef struct{
    uint8_t len;
    uint8_t data[FFS_PAGE_DATA_SIZE];
    uint16_t crc;
} ffs_page_t;


void ffs_page_v_reset( void );
void ffs_page_v_init( void );

uint16_t ffs_page_u16_total_pages( void );

uint8_t ffs_page_u8_count_files( void );
ffs_file_t ffs_page_i8_file_scan( ffs_file_t file_id );
int32_t ffs_page_i32_scan_file_size( ffs_file_t file_id );


ffs_file_t ffs_page_i8_create_file( void );
int8_t ffs_page_i8_delete_file( ffs_file_t file_id );
int32_t ffs_page_i32_file_size( ffs_file_t file_id );

int8_t ffs_page_i8_alloc_block( ffs_file_t file_id );
block_t ffs_page_i16_replace_block( ffs_file_t file_id, uint8_t file_block );
int8_t ffs_page_i8_block_copy( block_t source_block, block_t dest_block );
int32_t ffs_page_i32_alloc_page( ffs_file_t file_id, uint16_t page );
int32_t ffs_page_i32_seek_page( ffs_file_t file_id, uint16_t page );
int8_t ffs_page_i8_read( ffs_file_t file_id, uint16_t page, ffs_page_t *page_data );
int8_t ffs_page_i8_write( ffs_file_t file_id, uint16_t page, ffs_page_t *page_data );


#endif

