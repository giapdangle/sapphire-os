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
#include "crc.h"
#include "system.h"
#include "statistics.h"
#include "memory.h"

#include "ffs_page.h"
#include "ffs_block.h"
#include "ffs_global.h"
#include "flash25.h"
#include "flash_fs_partitions.h"


typedef struct{
	block_t next_block;
} block_info_t;

static mem_handle_t blocks_h;
static uint16_t total_blocks;

static block_t free_list;
static block_t dirty_list;

static uint16_t free_blocks;
static uint16_t dirty_blocks;


static block_info_t *get_block_ptr( void ){
    
    return (block_info_t *)mem2_vp_get_ptr( blocks_h );
}


void ffs_block_v_init( void ){
    
    blocks_h = -1;
    total_blocks = 0;
    
    // initialize data structures
	free_list = -1;
	dirty_list = -1;
	
	free_blocks = 0;
	dirty_blocks = 0;

    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        
        return;
    }

    // check size of flash device
    uint32_t flash_size = flash25_u32_capacity();
    uint16_t n_blocks = flash_size / FLASH_FS_ERASE_BLOCK_SIZE;
    
    total_blocks = n_blocks - ( FLASH_FS_FIRMWARE_0_N_BLOCKS + 1 );
    
    // allocate memory
    blocks_h = mem2_h_alloc( total_blocks * sizeof(block_info_t) );
    
    ASSERT( blocks_h >= 0 );

    block_info_t *blocks = get_block_ptr();

	for( uint16_t i = 0; i < total_blocks; i++ ){
		
		blocks[i].next_block = -1;
	}

    ffs_block_meta_t meta;

    // scan and verify all block headers
    for( uint16_t i = 0; i < total_blocks; i++ ){
        
        // read header flags
        uint8_t flags = ffs_block_u8_read_flags( i );

        // check if free
        if( flags == FFS_FLAGS_FREE ){
            
            // add to free list
            ffs_block_v_add_to_list( &free_list, i );
        }
        // check if marked as dirty
        else if( ~flags & FFS_FLAG_DIRTY ){
            
            // add to dirty list
            ffs_block_v_add_to_list( &dirty_list, i );
        }
        // check if marked as valid
        else if( flags == (uint8_t)~FFS_FLAG_VALID ){

            // read meta header and check for read errors
            if( ffs_block_i8_read_meta( i, &meta ) < 0 ){
                
                // read error, mark as dirty
                ffs_block_v_set_dirty( i );

                continue;
            }
        }
        else{ // flags are marked as something else
            
            // read error, mark as dirty
            ffs_block_v_set_dirty( i );
        }
    }

    // verify free space
    ffs_block_i8_verify_free_space();
}

int8_t ffs_block_i8_verify_free_space( void ){
    
    block_t block = free_list;
    
    bool errors = FALSE;
    
    while( block >= 0 ){
        
        // check all data in block
        uint8_t data[256];
        uint32_t addr = FFS_BLOCK_ADDRESS( block );
        
        for( uint8_t j = 0; j < FLASH_FS_ERASE_BLOCK_SIZE / 256; j++ ){
            
            flash25_v_read( addr, data, sizeof(data) );
            
            for( uint16_t h = 0; h < sizeof(data); h++ ){
                
                if( data[h] != 0xff ){
                    
                    errors = TRUE;
                    
                    ffs_block_v_remove_from_list( &free_list, block );
                    ffs_block_v_set_dirty( block );

                    goto scan_next_block;
                }
            }

            addr += sizeof(data);
        }
        
scan_next_block:
        
        block = ffs_block_i16_next( block );
    }

    if( errors ){
        
        return FFS_STATUS_ERROR;
    }

    return FFS_STATUS_OK;
}

// allocate a free block.
// note if the caller doesn't use the block, it is effectively removed from the file system until a remount
block_t ffs_block_i16_alloc( void ){
    
    if( free_blocks == 0 ){
        
        return FFS_STATUS_NO_FREE_SPACE;
    }
    
	block_t block = free_list;
	
    block_info_t *blocks = get_block_ptr();
	free_list = blocks[block].next_block;
	
	blocks[block].next_block = -1;
	
	free_blocks--;

    // return block
    return block;
}

block_t ffs_block_i16_get_dirty( void ){
    
    if( dirty_blocks == 0 ){
        
        return -1;
    }
    
	block_t block = dirty_list;
	
    block_info_t *blocks = get_block_ptr();
	dirty_list = blocks[block].next_block;
	
	blocks[block].next_block = -1;
	
	dirty_blocks--;

    // return block
    return block;
}



void ffs_block_v_add_to_list( block_t *head, block_t block ){
	
    // ensure block will mark the end of the list
    block_info_t *blocks = get_block_ptr();
    blocks[block].next_block = -1;

	// check if list is empty
	if( *head < 0 ){
		
		*head = block;
	}
    else{ // need to walk to end of list

        block_t cur_block = *head;
        
        //ASSERT( head < FFS_FILE_SYSTEM_N_BLOCKS ); // this gets checked in the loop
        ASSERT( block < (block_t)total_blocks );
        
        while(1){
            
            ASSERT( cur_block < (block_t)total_blocks );
            
            if( blocks[cur_block].next_block == -1 ){
                
                // add to list
                blocks[cur_block].next_block = block;
                
                break;
            }
            
            // advance
            cur_block = blocks[cur_block].next_block;
        }
    }

    // check list
    if( head == &dirty_list ){
        
        // increment count
        dirty_blocks++;
    }
    else if( head == &free_list ){
        
        // increment count
        free_blocks++;
    }
}

void ffs_block_v_replace_in_list( block_t *head, block_t old_block, block_t new_block ){
    
	ASSERT( old_block < (block_t)total_blocks );
	ASSERT( new_block < (block_t)total_blocks );

    block_info_t *blocks = get_block_ptr();

	if( *head == old_block ){
		
		*head = new_block;
	}
	else{
		
		block_t prev_block = *head;
		
		// seek to the block before old_block
		while( blocks[prev_block].next_block != old_block ){
		   
			prev_block = blocks[prev_block].next_block;
			
			ASSERT( prev_block >= 0 );
		}
		
		blocks[prev_block].next_block = new_block;
	}

	blocks[new_block].next_block = blocks[old_block].next_block;
}

void ffs_block_v_remove_from_list( block_t *head, block_t block ){
    
	ASSERT( block < (block_t)total_blocks );
    
    block_info_t *blocks = get_block_ptr();

	if( *head == block ){
		
		*head = blocks[block].next_block;
	}
	else{
		
		block_t prev_block = *head;
		
		// seek to the block
		while( blocks[prev_block].next_block != block ){
		   
			prev_block = blocks[prev_block].next_block;
			
			ASSERT( prev_block >= 0 );
		}
		
        blocks[prev_block].next_block = blocks[block].next_block;
	}

    // check list
    if( head == &dirty_list ){
        
        // decrement count
        dirty_blocks--;
    }
    else if( head == &free_list ){
        
        // decrement count
        free_blocks--;
    }
}

bool ffs_block_b_is_block_free( block_t block ){
    
    return ffs_block_b_is_in_list( &free_list, block );
}

bool ffs_block_b_is_block_dirty( block_t block ){
    
    return ffs_block_b_is_in_list( &dirty_list, block );
}

bool ffs_block_b_is_in_list( block_t *head, block_t block ){
    
    block_t cur_block = *head;
    
    block_info_t *blocks = get_block_ptr();
    
    //ASSERT( head < total_blocks ); // this gets checked in the loop
	ASSERT( block < (block_t)total_blocks );
	
	while( cur_block >= 0 ){
		
		ASSERT( cur_block < (block_t)total_blocks );
		
        if( cur_block == block ){
            
            return TRUE;
        }
        
		// advance
		cur_block = blocks[cur_block].next_block;
	}
    
    return FALSE;
}

// return size of list
uint8_t ffs_block_u8_list_size( block_t *head ){

    block_t cur_block = *head;
    uint8_t count = 0;
    block_info_t *blocks = get_block_ptr();
	
    while( cur_block >= 0 ){
		
		ASSERT( cur_block < (block_t)total_blocks );
        
		// advance
		cur_block = blocks[cur_block].next_block;

        count++;
	}
    
    return count;
}

block_t ffs_block_i16_get_block( block_t *head, uint8_t index ){
    
	ASSERT( index < total_blocks );
    
    block_t cur_block = *head;
    block_info_t *blocks = get_block_ptr();
    
    // iterate until we get to the block we want, or the end of the list
    while( ( index > 0 ) && ( cur_block >= 0 ) ){
        
        cur_block = blocks[cur_block].next_block;

        index--;
    }

    return cur_block;
}

block_t ffs_block_i16_next( block_t block ){
    
	ASSERT( block < (block_t)total_blocks );
    block_info_t *blocks = get_block_ptr();

    return blocks[block].next_block;
}


uint8_t ffs_block_u8_read_flags( block_t block ){

    ASSERT( block < (block_t)total_blocks );
    
    uint8_t tries = FFS_IO_ATTEMPTS;
    
    while( tries > 0 ){
        
        tries--;
        
        // read both sets of flags
        uint8_t flags0 = flash25_u8_read_byte( FFS_META_0( block ) + offsetof(ffs_block_meta_t, flags) );
        uint8_t flags1 = flash25_u8_read_byte( FFS_META_1( block ) + offsetof(ffs_block_meta_t, flags) );
        
        // check for match
        if( flags0 == flags1 ){
            
            return flags0;
        }
    }
    
    return FFS_FLAGS_INVALID; // this indicates flags are invalid
}

void ffs_block_v_set_dirty( block_t block ){
    
    ASSERT( block < (block_t)total_blocks );
    
    // read flags
    uint8_t flags = ffs_block_u8_read_flags( block );
    
    // set dirty
    flags &= ~FFS_FLAG_DIRTY;
    
    // write new flags byte to both headers
    flash25_v_write_byte( FFS_META_0( block ) + offsetof(ffs_block_meta_t, flags), flags );
    flash25_v_write_byte( FFS_META_1( block ) + offsetof(ffs_block_meta_t, flags), flags );
    
    // read back to test
    // we'll accept just one flags being correct, since either way will result in a block erasure
    if( ( flash25_u8_read_byte( FFS_META_0( block ) + offsetof(ffs_block_meta_t, flags) ) != flags ) &&
        ( flash25_u8_read_byte( FFS_META_1( block ) + offsetof(ffs_block_meta_t, flags) ) != flags ) ){
        
        // neither set of flags are correct
        // possible write error on this block
        
        // try setting flag to invalid, and hope for the best
        flash25_v_write_byte( FFS_META_0( block ) + offsetof(ffs_block_meta_t, flags), FFS_FLAGS_INVALID );
        flash25_v_write_byte( FFS_META_1( block ) + offsetof(ffs_block_meta_t, flags), FFS_FLAGS_INVALID );
    }

    // add to the dirty list
	ffs_block_v_add_to_list( &dirty_list, block );
}

bool ffs_block_b_is_dirty( const ffs_block_meta_t *meta ){
    
    return ( ( ~meta->flags & FFS_FLAG_DIRTY ) != 0 );
}

int8_t ffs_block_i8_erase( block_t block ){
    
    // enable writes
    flash25_v_write_enable();
    
    // erase it
    flash25_v_erase_4k( FFS_BLOCK_ADDRESS( block ) );

    // spin lock until erase is finished
    while( flash25_b_busy() );

    // add to free list
    ffs_block_v_add_to_list( &free_list, block );

    return FFS_STATUS_OK;
}

uint16_t ffs_block_u16_free_blocks( void ){
    
    return free_blocks;
}

uint16_t ffs_block_u16_dirty_blocks( void ){
    
    return dirty_blocks;
}

uint16_t ffs_block_u16_total_blocks( void ){
    
    return total_blocks;
}

int8_t ffs_block_i8_read_meta( block_t block, ffs_block_meta_t *meta ){
    
    ffs_block_meta_t check_meta;
    ffs_block_meta_t check_meta_1;

    ASSERT( block < (block_t)total_blocks );
    
    uint8_t tries = FFS_IO_ATTEMPTS;
    
    while( tries > 0 ){
        
        tries--;

        // read both headers
        flash25_v_read( FFS_META_0(block), &check_meta, sizeof(ffs_block_meta_t) );
        flash25_v_read( FFS_META_1(block), &check_meta_1, sizeof(ffs_block_meta_t) );
        
        // compare
        if( memcmp( &check_meta, &check_meta_1, sizeof(ffs_block_meta_t) ) != 0 ){
            
            // soft error, we'll retry
            ffs_block_v_soft_error();

            continue;
        }

        // sanity checks
        if( ( check_meta.file_id >= FFS_MAX_FILES ) ||
            ( check_meta.block >= total_blocks ) ){
            
            // header has bad data
            ffs_block_v_soft_error();

            continue;
        }

        // header is ok
        // check if caller wants the actual data
        if( meta != 0 ){
            
            *meta = check_meta;
        }

        return FFS_STATUS_OK;
    }
    
    // hard error, could not reliably read header
    ffs_block_v_hard_error();

    return FFS_STATUS_ERROR;
}   

int8_t ffs_block_i8_write_meta( block_t block, const ffs_block_meta_t *meta ){

    // write to flash
    flash25_v_write( FFS_META_0(block), meta, sizeof(ffs_block_meta_t) );
    flash25_v_write( FFS_META_1(block), meta, sizeof(ffs_block_meta_t) );
    
    // read back to test
    if( ffs_block_i8_read_meta( block, 0 ) < 0 ){
        
        return FFS_STATUS_ERROR;
    }
    
    return FFS_STATUS_OK;
}


int8_t ffs_block_i8_read_index( block_t block, ffs_block_index_t *index ){
    
    ffs_block_index_t check_index;
    ffs_block_index_t check_index_1;

    ASSERT( block < (block_t)total_blocks );
    
    uint8_t tries = FFS_IO_ATTEMPTS;
    
    while( tries > 0 ){
        
        tries--;

        // read both headers
        flash25_v_read( FFS_INDEX_0(block), &check_index, sizeof(ffs_block_index_t) );
        flash25_v_read( FFS_INDEX_1(block), &check_index_1, sizeof(ffs_block_index_t) );
        
        // compare
        if( memcmp( &check_index, &check_index_1, sizeof(ffs_block_index_t) ) != 0 ){
            
            // soft error, we'll retry
            ffs_block_v_soft_error();

            continue;
        }

        // header is ok
        // check if caller wants the actual data
        if( index != 0 ){
            
            *index = check_index;
        }

        return FFS_STATUS_OK;
    }
    
    // hard error, could not reliably read index
    ffs_block_v_hard_error();

    return FFS_STATUS_ERROR;
}   

int8_t ffs_block_i8_set_index_entry( block_t block, uint8_t logical_index, uint8_t phy_index ){
    
    ASSERT( logical_index < FFS_DATA_PAGES_PER_BLOCK );
    ASSERT( phy_index < FFS_PAGES_PER_BLOCK );

    // write index entry
    flash25_v_write_byte( FFS_INDEX_0(block) + phy_index, logical_index );
    flash25_v_write_byte( FFS_INDEX_1(block) + phy_index, logical_index );
    
    // read back
    if( ( flash25_u8_read_byte( FFS_INDEX_0(block) + phy_index ) != logical_index ) ||
        ( flash25_u8_read_byte( FFS_INDEX_1(block) + phy_index ) != logical_index ) ){
        
        return FFS_STATUS_ERROR;
    }

    return FFS_STATUS_OK;
}

int8_t ffs_block_i8_get_index_info( block_t block, ffs_index_info_t *info ){
    
    ffs_block_index_t index;

    // read index
    if( ffs_block_i8_read_index(block, &index ) < 0 ){
        
        return FFS_STATUS_ERROR;
    }
    
    // init info
    info->data_pages        = 0;
    info->free_pages        = 0;
    info->phy_last_page     = -1;
    info->logical_last_page = -1;
    info->phy_next_free     = -1;
    
    // scan index
    for( uint8_t i = 0; i < FFS_PAGES_PER_BLOCK; i++ ){
        
        // check if entry is free
        if( index.page_index[i] == 0xff ){
            
            info->free_pages++;
            
            // check if we need to mark a free page
            if( info->phy_next_free < 0 ){
                
                info->phy_next_free = i;
            }
        }
        // check if newer last logical page is found
        else if( index.page_index[i] >= info->logical_last_page ){
            
            info->logical_last_page     = index.page_index[i];
            info->phy_last_page         = i;
        }
    }
    
    // count data pages
    info->data_pages = info->logical_last_page + 1;
    
    return FFS_STATUS_OK;
}

void ffs_block_v_soft_error( void ){

    stats_v_increment( STAT_FLASH_FS_SOFT_IO_ERRORS );
}

void ffs_block_v_hard_error( void ){

    stats_v_increment( STAT_FLASH_FS_HARD_IO_ERRORS );

    sys_v_set_warnings( SYS_WARN_FLASHFS_HARD_ERROR );
}



