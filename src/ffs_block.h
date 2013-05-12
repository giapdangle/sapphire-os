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

#ifndef _FFS_BLOCK_H
#define _FFS_BLOCK_H

#include "ffs_global.h"

// flags
#define FFS_FLAG_DIRTY          0x80 // 0 if block is dirty
#define FFS_FLAG_VALID          0x40 // 0 if block is valid

#define FFS_FLAGS_INVALID   0
#define FFS_FLAGS_FREE      0xff

typedef int16_t block_t;

// macro to compute a block address from an FS block number
#define FFS_BLOCK_ADDRESS(block_num) ( ( (uint32_t)block_num * (uint32_t)FLASH_FS_ERASE_BLOCK_SIZE ) + (uint32_t)FLASH_FS_FILE_SYSTEM_START )

typedef struct{
    uint8_t file_id;
    uint8_t flags;
    uint8_t block;
    uint8_t sequence;
    uint8_t reserved[4];
} ffs_block_meta_t;

typedef struct{
    uint8_t page_index[FFS_PAGES_PER_BLOCK];
} ffs_block_index_t;

typedef struct{
    ffs_block_meta_t meta;
    ffs_block_index_t index;
} ffs_block_header_t;

#define FFS_META_0(block) ( FFS_BLOCK_ADDRESS(block) )
#define FFS_META_1(block) ( FFS_META_0(block) + sizeof(ffs_block_header_t) )

#define FFS_INDEX_0(block) ( FFS_META_0(block) + sizeof(ffs_block_meta_t) )
#define FFS_INDEX_1(block) ( FFS_INDEX_0(block) + sizeof(ffs_block_header_t) )

typedef struct{
    uint8_t data_pages;
    uint8_t free_pages;
    int8_t phy_last_page;
    int8_t logical_last_page;
    int8_t phy_next_free;
} ffs_index_info_t;


// public API:
void ffs_block_v_init( void );

int8_t ffs_block_i8_verify_free_space( void );
block_t ffs_block_i16_alloc( void );
block_t ffs_block_i16_get_dirty( void );
void ffs_block_v_add_to_list( block_t *head, block_t block );
void ffs_block_v_replace_in_list( block_t *head, block_t old_block, block_t new_block );
void ffs_block_v_remove_from_list( block_t *head, block_t block );

bool ffs_block_b_is_block_free( block_t block );
bool ffs_block_b_is_block_dirty( block_t block );
bool ffs_block_b_is_in_list( block_t *head, block_t block );
uint8_t ffs_block_u8_list_size( block_t *head );
block_t ffs_block_i16_get_block( block_t *head, uint8_t index );
block_t ffs_block_i16_next( block_t block );

uint8_t ffs_block_u8_read_flags( block_t block );
void ffs_block_v_set_dirty( block_t block );
bool ffs_block_b_is_dirty( const ffs_block_meta_t *meta );
int8_t ffs_block_i8_erase( block_t block );

uint16_t ffs_block_u16_free_blocks( void );
uint16_t ffs_block_u16_dirty_blocks( void );
uint16_t ffs_block_u16_total_blocks( void );

int8_t ffs_block_i8_read_meta( block_t block, ffs_block_meta_t *meta );
int8_t ffs_block_i8_write_meta( block_t block, const ffs_block_meta_t *meta );
int8_t ffs_block_i8_read_index( block_t block, ffs_block_index_t *index );
int8_t ffs_block_i8_set_index_entry( block_t block, uint8_t logical_index, uint8_t phy_index );
int8_t ffs_block_i8_get_index_info( block_t block, ffs_index_info_t *info );

void ffs_block_v_soft_error( void );
void ffs_block_v_hard_error( void );


#endif

