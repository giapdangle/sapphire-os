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

#include "flash25.h"
#include "ffs_global.h"
#include "ffs_block.h"
#include "flash_fs_partitions.h"

#include "ffs_page.h"


typedef struct{
	block_t start_block;
	int32_t size;
} file_info_t;

static file_info_t files[FFS_MAX_FILES];


// returns TRUE if seq1 is newer than seq2
bool compare_sequences( uint8_t seq1, uint8_t seq2 ){
    
    return ( (int8_t)( seq1 - seq2 ) ) > 0;
}

int32_t page_address( block_t block, uint8_t page_index ){
    
    // calculate page address
    int32_t addr = FFS_BLOCK_ADDRESS( block ); // block base address
    addr += ( sizeof(ffs_block_header_t) * 2 ); // offset to first page
    
    // offset to page index
    addr += ( sizeof(ffs_page_t) * page_index );
    
    return addr;
}

void ffs_page_v_reset( void ){
    
    // initialize data structures
    for( uint8_t i = 0; i < FFS_MAX_FILES; i++ ){
        
        files[i].start_block = -1;
        files[i].size = -1;
    }
}

void ffs_page_v_init( void ){
    
    ffs_page_v_reset();

    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        
        return;
    }

    ffs_block_meta_t meta;

    // scan for files
    for( uint16_t i = 0; i < ffs_block_u16_total_blocks(); i++ ){

        // read header flags
        uint8_t flags = ffs_block_u8_read_flags( i );

        // check if marked as valid
        if( flags == (uint8_t)~FFS_FLAG_VALID ){
            
            // read meta header and check for read errors
            if( ffs_block_i8_read_meta( i, &meta ) < 0 ){
                
                // read error, mark as dirty
                ffs_block_v_set_dirty( i );

                continue;
            }

            // meta data verified
            
            // add to file list
            ffs_block_v_add_to_list( &files[meta.file_id].start_block, i );
        }

        // ignore invalid blocks, the block driver will handle those
    }    
    
    // file data scan
    for( uint8_t file = 0; file < FFS_MAX_FILES; file++ ){
    
scan_start:

        // check if file exists
        if( files[file].start_block < 0 ){
            
            continue;
        }
        
        // create block map for file
        block_t file_map[ffs_block_u16_total_blocks()];
        memset( file_map, -1, sizeof(file_map) );
        
        // iterate over file list and map block numbers
        block_t block = files[file].start_block;
        
        while( block >= 0 ){
            
            // read meta header.
            // we've already scanned the meta data once and verified the data is intact, so we'll
            // assume the data is valid here.
            ASSERT( ffs_block_i8_read_meta( block, &meta ) == 0 );
            
            // check if this block number was already found
            if( file_map[meta.block] >= 0 ){
                
                // read meta from prev block
                ffs_block_meta_t prev_meta;
                
                // same notes as above
                ASSERT( ffs_block_i8_read_meta( block, &prev_meta ) == 0 );
                
                // compare sequences
                if( compare_sequences( meta.sequence, prev_meta.sequence ) ){
                    
                    // current block is newer, set old block to dirty
                    ffs_block_v_set_dirty( file_map[meta.block] );

                    // assign new block
                    file_map[meta.block] = block;
                }
                else{
                    
                    // we already have the most current block, set the one we just scanned to dirty
                    ffs_block_v_set_dirty( block );
                }
            }
            else{
                
                file_map[meta.block] = block;
            }
            
            // get next block
            block = ffs_block_i16_next( block );
        }
        
        // now we have file map and we've resolved duplicates
        // let's check for missing blocks
        // we'll treat that is a file error and delete the entire file
        for( uint16_t i = 0; i < ( ffs_block_u16_total_blocks() - 1 ); i++ ){
            
            // check if current block is missing but there is a next block
            if( ( file_map[i] == -1 ) && ( file_map[i + 1] >= 0 ) ){
                
                // delete file
                ffs_page_i8_delete_file( file );
                
                goto scan_start;
            }
        }
        
        // no duplicate blocks, no missing blocks.  rebuild the list in order.
        files[file].start_block = -1;

        for( uint16_t i = 0; i < ffs_block_u16_total_blocks(); i++ ){
            
            // check for end of file
            if( file_map[i] < 0 ){
                
                break;
            }
            
            // add to list
            ffs_block_v_add_to_list( &files[file].start_block, file_map[i] );
        }
        
        // set file size
        files[file].size = ffs_page_i32_scan_file_size( file );

        // check for error
        if( files[file].size < 0 ){
            
            ffs_page_i8_delete_file( file );
        }

        // scan file data
        if( ffs_page_i8_file_scan( file ) < 0 ){
            
            // data integrity error, trash file
            ffs_page_i8_delete_file( file );
            
            goto scan_start;
        }
    }
}

uint16_t ffs_page_u16_total_pages( void ){
    
    return ffs_block_u16_total_blocks() * FFS_PAGES_PER_BLOCK;
}

uint8_t ffs_page_u8_count_files( void ){
    
    uint8_t count = 0;

    for( uint8_t i = 0; i < FFS_MAX_FILES; i++ ){
        
        if( files[i].size >= 0 ){
            
            count++;
        }
    }

    return count;
}

int8_t ffs_page_i8_file_scan( ffs_file_t file_id ){
    
    ASSERT( file_id < FFS_MAX_FILES );
    
    // calculate number of pages
    uint16_t pages = files[file_id].size / FFS_PAGE_DATA_SIZE;

    if( ( files[file_id].size % FFS_PAGE_DATA_SIZE ) > 0 ){
        
        pages++;
    }
    
    // special case, file is empty
    if( files[file_id].size == 0 ){
            
        // check that we don't have a page 0
        if( ffs_page_i32_seek_page( file_id, 0 ) != FFS_STATUS_EOF ){
            
            return FFS_STATUS_ERROR;
        }

        return FFS_STATUS_OK;
    }
    
    // iterate over all pages and verify all data
    for( uint16_t i = 0; i < pages; i++ ){
        
        ffs_page_t page;

        if( ffs_page_i8_read( file_id, i, &page ) < 0 ){
            
            return FFS_STATUS_ERROR;
        }
        
        // check length (except on last page)
        if( i < ( pages - 1 ) ){
            
            if( page.len != FFS_PAGE_DATA_SIZE ){
                
                return FFS_STATUS_ERROR;
            }
        }
    }
    
    return FFS_STATUS_OK;
}


int32_t ffs_page_i32_scan_file_size( ffs_file_t file_id ){
    
    ASSERT( file_id < FFS_MAX_FILES );

    uint8_t block_count = ffs_block_u8_list_size( &files[file_id].start_block );
    
    ASSERT( block_count > 0 );
    
    uint8_t file_block_number = block_count - 1;

    ffs_index_info_t index_info;
    
    block_t block = ffs_block_i16_get_block( &files[file_id].start_block, file_block_number );
    
    // check if block was found
    if( block < 0 ){
        
        // file is empty
        return 0;
    }

    // read index info for last block in file
    if( ffs_block_i8_get_index_info( block, &index_info ) <  0 ){
        
        return FFS_STATUS_ERROR;
    }
    
    // check if there are data pages in this block
    if( index_info.data_pages == 0 ){
        
        // check if there is a previous block to try
        if( file_block_number > 0 ){

            // try previous block
            file_block_number--;
            block = ffs_block_i16_get_block( &files[file_id].start_block, file_block_number );

            ASSERT( block >= 0 );           

            if( ffs_block_i8_get_index_info( block, &index_info ) <  0 ){
                
                return FFS_STATUS_ERROR;
            }
        }
    }
    
    // check last page
    if( index_info.logical_last_page < 0 ){
        
        // file is empty
        return 0;
    }
    
    // calculate logical file page number
    uint16_t last_page = ( file_block_number * FFS_DATA_PAGES_PER_BLOCK ) + index_info.logical_last_page;
    
    // read data page
    ffs_page_t page;
    
    if( ffs_page_i8_read( file_id, last_page, &page ) < 0 ){
        
        return FFS_STATUS_ERROR;
    }
    
    // return calculated file size
    return ( (uint32_t)last_page * (uint32_t)FFS_PAGE_DATA_SIZE ) + (uint32_t)page.len;
}


ffs_file_t ffs_page_i8_create_file( void ){
    
    ffs_file_t file = -1;

    // get a free file id
    for( uint8_t i = 0; i < FFS_MAX_FILES; i++ ){
        
        // check if ID is free
        if( files[i].size < 0 ){
            
            // assign ID
            file = i;

            break;
        }
    }

    // was there a free ID?
    if( file < 0 ){
        
        return FFS_STATUS_NO_FREE_FILES;
    }
    
    // allocate a block
    block_t block = ffs_page_i8_alloc_block( file );
    
    // check status
    if( block < 0 ){
        
        return block; // error code
    }
    
    // set file size
    files[file].size        = 0;

    // return file ID
    return file;
}

int8_t ffs_page_i8_delete_file( ffs_file_t file_id ){
    
    ASSERT( file_id < FFS_MAX_FILES );
    
    // set all file blocks to dirty
    block_t block = ffs_block_i16_get_block( &files[file_id].start_block, 0 );
    
    while( block >= 0 ){
        
        // get next block first
        block_t next = ffs_block_i16_next( block );

        ffs_block_v_set_dirty( block );
        
        block = next;
    }
    
    // clear file entry
    files[file_id].start_block  = -1;
    files[file_id].size         = -1;
    
    return FFS_STATUS_OK;
}

int32_t ffs_page_i32_file_size( ffs_file_t file_id ){
    
    ASSERT( file_id < FFS_MAX_FILES );
    
    return files[file_id].size;
}

// allocate a new block and add it to the end of the file
int8_t ffs_page_i8_alloc_block( ffs_file_t file_id ){
    
    ASSERT( file_id < FFS_MAX_FILES );
    
    // get current block count for file
    uint8_t block_count = ffs_block_u8_list_size( &files[file_id].start_block );
    
    // set up retry loop
    uint8_t tries = FFS_IO_ATTEMPTS;
    
    while( tries > 0 ){
        
        tries--;
    
        // allocate a block to the file
        block_t block = ffs_block_i16_alloc();
        
        // check if there was a free block
        if( block < 0 ){
            
            return FFS_STATUS_NO_FREE_SPACE;
        }
        
        // set up block header
        ffs_block_meta_t meta;
        meta.file_id    = file_id;
        meta.flags      = ~FFS_FLAG_VALID;
        meta.block      = block_count;
        meta.sequence   = 0;
        memset( meta.reserved, 0xff, sizeof(meta.reserved) );
        
        // write meta data to block and check for errors
        if( ffs_block_i8_write_meta( block, &meta ) == 0 ){
            
            // add to file list
            ffs_block_v_add_to_list( &files[file_id].start_block, block );

            // success
            return FFS_STATUS_OK;
        }
        else{
            
            ffs_block_v_soft_error();

            // write error, set block to dirty
            ffs_block_v_set_dirty( block );
        }
    }

    ffs_block_v_hard_error();

    return FFS_STATUS_ERROR;
}

block_t ffs_page_i16_replace_block( ffs_file_t file_id, uint8_t file_block ){
    
    ASSERT( file_id < FFS_MAX_FILES );

    // map logical file block to physical block
    block_t phy_block = ffs_block_i16_get_block( &files[file_id].start_block, file_block );
    
    ASSERT( phy_block >= 0 );

    uint8_t tries = FFS_IO_ATTEMPTS;
    
    while( tries > 0 ){
        
        tries--;

        // try allocating a new block
        block_t new_block = ffs_block_i16_alloc();

        if( new_block < 0 ){
            
            return FFS_STATUS_NO_FREE_SPACE;
        }
        
        // copy old block to new
        if( ffs_page_i8_block_copy( phy_block, new_block ) < 0 ){
            
            // fail
            ffs_block_v_set_dirty( new_block );
            
            ffs_block_v_soft_error();

            // retry
            continue;
        }
        
        // replace block in file list
        ffs_block_v_replace_in_list( &files[file_id].start_block, phy_block, new_block );   

        // set old block to dirty
        ffs_block_v_set_dirty( phy_block );
    
        // return new phy block
        return new_block;
    }

    ffs_block_v_hard_error();

    return FFS_STATUS_ERROR;
}

int8_t ffs_page_i8_block_copy( block_t source_block, block_t dest_block ){
    
    ffs_block_meta_t meta;
    ffs_page_t page;
    ffs_page_t page_check;

    // read source meta
    if( ffs_block_i8_read_meta( source_block, &meta ) < 0 ){
        
        // read error
        return FFS_STATUS_ERROR;
    }
    
    // calc base page number
    uint16_t base_page = meta.block * FFS_DATA_PAGES_PER_BLOCK;
    
    // iterate through source pages
    for( uint8_t i = 0; i < FFS_DATA_PAGES_PER_BLOCK; i++ ){
        
        // read page
        int8_t status = ffs_page_i8_read( meta.file_id, base_page + i, &page );
        
        // check EOF
        if( status == FFS_STATUS_EOF ){
            
            break;
        }

        // check error
        if( status < 0 ){
           
            return FFS_STATUS_ERROR;
        }
        
        // update dest index
        if( ffs_block_i8_set_index_entry( dest_block, i, i ) < 0 ){
            
            return FFS_STATUS_ERROR;
        }

        // write page data
        flash25_v_write( page_address( dest_block, i ), &page, sizeof(page) );

        // read back
        flash25_v_read( page_address( dest_block, i ), &page_check, sizeof(page_check) );
        
        // compare
        if( memcmp( &page, &page_check, sizeof(page) ) != 0 ){
            
            return FFS_STATUS_ERROR;
        }
    }
    
    // increment block sequence
    meta.sequence++;
    
    // write meta
    if( ffs_block_i8_write_meta( dest_block, &meta ) < 0 ){
        
        return FFS_STATUS_ERROR;
    }

    return FFS_STATUS_OK;
}


int32_t ffs_page_i32_alloc_page( ffs_file_t file_id, uint16_t page ){
    
    ASSERT( file_id < FFS_MAX_FILES );
    ASSERT( page < ffs_page_u16_total_pages() );
    
    // calculate logical block number
    block_t block = page / FFS_DATA_PAGES_PER_BLOCK;
    uint8_t page_index = page % FFS_DATA_PAGES_PER_BLOCK;
    
    // get list size
    uint8_t block_count = ffs_block_u8_list_size( &files[file_id].start_block );
    
    // set page count, if file has a size greater than 0
    uint16_t page_count = 0;
    
    if( files[file_id].size > 0 ){
        
        page_count = ( files[file_id].size / FFS_PAGE_DATA_SIZE ) + 1;
    }

    // check for EOF
    if( page > page_count ){
        
        return FFS_STATUS_EOF;
    }

    // check if we're trying to write the next logical block
    if( block_count == block ){
        // Ex: we have 2 blocks, and we want a 3rd block (number 2)
            
        // allocate a new block
        block_t new_block = ffs_page_i8_alloc_block( file_id );

        // check status
        if( new_block < 0 ){
          
            // return error code
            return new_block;
        }
    }

    // seek physical block number in file block list
    block_t phy_block = ffs_block_i16_get_block( &files[file_id].start_block, block );
    
    ASSERT( phy_block >= 0 );
    
    // get index info for block
    ffs_index_info_t index_info;

    if( ffs_block_i8_get_index_info( phy_block, &index_info ) < 0 ){
        
        return FFS_STATUS_ERROR;
    }
    
    // check for free page
    if( index_info.phy_next_free < 0 ){
        
        // this block is full
        // try replacing with a new block
        phy_block = ffs_page_i16_replace_block( file_id, block );
        
        // check error code
        if( phy_block < 0 ){
            
            return phy_block;
        }
        
        // get new index info
        if( ffs_block_i8_get_index_info( phy_block, &index_info ) < 0 ){
            
            return FFS_STATUS_ERROR;
        }
    }
    
    // update index
    // use a retry loop, since this could fail
    uint8_t tries = FFS_IO_ATTEMPTS;
    
    while( tries > 0 ){
        
        tries--;

        if( index_info.phy_next_free < 0 ){
            
            return FFS_STATUS_ERROR;
        }       
        
        // update index
        if( ffs_block_i8_set_index_entry( phy_block, page_index, index_info.phy_next_free ) < 0 ){

            // if error, try next entry
            if( ffs_block_i8_get_index_info( phy_block, &index_info ) < 0 ){
                
                return FFS_STATUS_ERROR;
            }

            ffs_block_v_soft_error();

            continue;
        }
               
        // index update succeeded,
        // return physical address of the page
        return page_address( phy_block, index_info.phy_next_free );
    }
    
    ffs_block_v_hard_error();

    return FFS_STATUS_ERROR;
}

int32_t ffs_page_i32_seek_page( ffs_file_t file_id, uint16_t page ){
    
    ASSERT( file_id < FFS_MAX_FILES );
    ASSERT( page < ffs_page_u16_total_pages() );

    // calculate logical block number
    uint8_t block = page / FFS_DATA_PAGES_PER_BLOCK;
    uint8_t page_index = page % FFS_DATA_PAGES_PER_BLOCK;
       
    // seek physical block number in file block list
    block_t phy_block = ffs_block_i16_get_block( &files[file_id].start_block, block );
    
    // check if block was found
    if( phy_block < 0 ){
        
        // assume this is end of file
        return FFS_STATUS_EOF;
    }

    ffs_block_index_t index;
    
    // read index from block
    if( ffs_block_i8_read_index( phy_block, &index ) < 0 ){
        
        // read error
        return FFS_STATUS_ERROR;
    }
    
    int8_t i = FFS_PAGES_PER_BLOCK - 1;

    // scan index backwards to get current physical page
    while( i >= 0 ){
        
        if( index.page_index[i] == page_index ){
            
            // calculate page address
            return page_address( phy_block, i );
        }

        i--;
    } 

    // page not found, assume end of file
    return FFS_STATUS_EOF;
}

int8_t ffs_page_i8_read( ffs_file_t file_id, uint16_t page, ffs_page_t *page_data ){
    
    // seek to page
    int32_t page_addr = ffs_page_i32_seek_page( file_id, page );
    
    // check error codes
    if( page_addr == FFS_STATUS_EOF ){
        
        return FFS_STATUS_EOF;
    }
    else if( page_addr < 0 ){
        
        return FFS_STATUS_ERROR;
    }
    
    // ok, we have the correct page address
    
    // set up retry loop
    uint8_t tries = FFS_IO_ATTEMPTS;
    
    while( tries > 0 ){
        
        tries--;
        
        // read page
        flash25_v_read( page_addr, page_data, sizeof(ffs_page_t) );
        
        // check crc
        if( crc_u16_block( page_data->data, page_data->len ) == page_data->crc ){
            
            return FFS_STATUS_OK;
        }

        ffs_block_v_soft_error();
    }
 
    ffs_block_v_hard_error();

    return FFS_STATUS_ERROR;
}


int8_t ffs_page_i8_write( ffs_file_t file_id, uint16_t page, ffs_page_t *page_data ){
    
    ffs_page_t check_page;

    // set up retry loop
    uint8_t tries = FFS_IO_ATTEMPTS;
    
    while( tries > 0 ){
        
        tries--;

        // allocate new page
        int32_t page_addr = ffs_page_i32_alloc_page( file_id, page );
        
        // check allocation
        if( page_addr < 0 ){
            
            // error, try again

            continue;
        }
        
        // calculate CRC
        page_data->crc = crc_u16_block( page_data->data, page_data->len );
        
        // write to flash
        flash25_v_write( page_addr, page_data, sizeof(ffs_page_t) );

        // read back
        flash25_v_read( page_addr, &check_page, sizeof(ffs_page_t) );

        // compare
        if( memcmp( &check_page, page_data, sizeof(ffs_page_t) ) == 0 ){
            
            // calculate file length up to this page plus the data in it
            uint32_t file_length_to_here = ( (uint32_t)page * (uint32_t)FFS_PAGE_DATA_SIZE ) + page_data->len;

            // check file size
            if( file_length_to_here > (uint32_t)files[file_id].size ){
                
                // adjust file size
                files[file_id].size = file_length_to_here;
            }

            // success
            return FFS_STATUS_OK;
        }

        ffs_block_v_soft_error();
    }

    ffs_block_v_hard_error();

    return FFS_STATUS_ERROR;
}



