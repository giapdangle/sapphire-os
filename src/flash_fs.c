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
#include "system.h"
#include "statistics.h"

#include "flash25.h"
#include "ffs_fw.h"
#include "ffs_page.h"
#include "ffs_block.h"
#include "ffs_gc.h"
#include "ffs_global.h"
#include "flash_fs_partitions.h"

#include "flash_fs.h"



void ffs_v_init( void ){
    
    COMPILER_ASSERT( sizeof(ffs_file_meta0_t) <= FFS_PAGE_DATA_SIZE );
    COMPILER_ASSERT( sizeof(ffs_file_meta1_t) <= FFS_PAGE_DATA_SIZE );
    
    flash25_device_info_t dev_info;
    
    // check if external flash is present and accessible
    flash25_v_read_device_info( &dev_info );
    
    // check mfg and dev id
    if( ( dev_info.mfg_id != FLASH_MFG_SST ) ||
        ( dev_info.dev_id_1 != FLASH_DEV_ID1_SST25 ) ){
        
        sys_v_set_warnings( SYS_WARN_FLASHFS_FAIL );
        
        return;
    }
    
    // check system mode
    if( sys_u8_get_mode() == SYS_MODE_FORMAT ){
        
        // format the file system.  this will also re-mount.
        ffs_v_format();

        // restart in normal mode
        sys_reboot();
    }

    ffs_fw_i8_init();

    ffs_v_mount();

    ffs_gc_v_init();
}


void ffs_v_mount( void ){
    
    ffs_block_v_init();
    ffs_page_v_init();
}

void ffs_v_format( void ){

	// enable writes
	flash25_v_write_enable();
	
	// erase entire array
    flash25_v_erase_chip();
	
	// wait until finished
	while( flash25_b_busy() ){
        
        wdt_reset();
    }
   
    // re-mount
    ffs_v_mount();
}


uint32_t ffs_u32_get_file_count( void ){
    
    return ffs_page_u8_count_files() + 1; // add 1 for firmware
}

uint32_t ffs_u32_get_dirty_space( void ){
    
    return (uint32_t)ffs_block_u16_dirty_blocks() * (uint32_t)FFS_BLOCK_DATA_SIZE;
}

uint32_t ffs_u32_get_free_space( void ){
    
    return (uint32_t)ffs_block_u16_free_blocks() * (uint32_t)FFS_BLOCK_DATA_SIZE;
}

uint32_t ffs_u32_get_available_space( void ){

    return ffs_u32_get_dirty_space() + ffs_u32_get_free_space();
}

uint32_t ffs_u32_get_total_space( void ){
    
	return (uint32_t)ffs_block_u16_total_blocks() * (uint32_t)FFS_BLOCK_DATA_SIZE;
}


int32_t ffs_i32_get_file_size( ffs_file_t file_id ){
    
    // special case for firmware partition
    if( file_id == FFS_FILE_ID_FIRMWARE ){
        
        return ffs_fw_u32_size();
    }

    ASSERT( file_id < FFS_MAX_FILES );

    int32_t raw_size = ffs_page_i32_file_size( file_id );
    
    // check for errors
    if( raw_size < 0 ){
        
        // return error from page driver
        return raw_size;
    }
    
    // check file size is valid (should have meta data)
    if( raw_size < FFS_FILE_META_SIZE ){
        
        return FFS_STATUS_ERROR;
    }
    
    // return data size
    return raw_size - FFS_FILE_META_SIZE;
}

int8_t ffs_i8_read_filename( ffs_file_t file_id, char *dst, uint8_t max_len ){
    
    // special case for firmware partition
    if( file_id == FFS_FILE_ID_FIRMWARE ){
        
        strncpy_P( dst, PSTR("firmware.bin"), max_len );

        return FFS_STATUS_OK;
    }

    ASSERT( file_id < FFS_MAX_FILES );

    ffs_page_t page;
    ffs_file_meta0_t *meta0 = (ffs_file_meta0_t *)page.data;
    
    // read meta0
    if( ffs_page_i8_read( file_id, FFS_FILE_PAGE_META_0, &page ) < 0 ){
        
        return FFS_STATUS_ERROR;
    }
    
    // copy filename
    strncpy( dst, meta0->filename, max_len );
    
    return FFS_STATUS_OK;
}

ffs_file_t ffs_i8_create_file( char filename[] ){
    
    // create file
    ffs_file_t file = ffs_page_i8_create_file();
    
    // check status
    if( file < 0 ){
        
        // return error code
        return file;
    }
    
    // we have a valid file
    
    // set up first set of meta data (filename)
    ffs_page_t page;
    ffs_file_meta0_t *meta0 = (ffs_file_meta0_t *)page.data;
    
    // start with 0s
    memset( page.data, 0, sizeof(page.data) );

    // copy file name
    strncpy( meta0->filename, filename, sizeof(meta0->filename) );
    page.len = sizeof(meta0->filename);

    // write to page 0
    if( ffs_page_i8_write( file, FFS_FILE_PAGE_META_0, &page ) < 0 ){
        
        // write error
        goto clean_up;
    }
    
    // set up second set of meta data (reserved, all 1s at this time)
    ffs_file_meta1_t *meta1 = (ffs_file_meta1_t *)page.data;
    
    // start with 1s
    memset( meta1->reserved, 0xff, sizeof(meta1->reserved) );
    page.len = sizeof(meta1->reserved);
    
    // write to page 1
    if( ffs_page_i8_write( file, FFS_FILE_PAGE_META_1, &page ) < 0 ){
        
        // write error
        goto clean_up;
    }

    // success
    return file;


// error handling
clean_up:
    
    // delete file
    ffs_page_i8_delete_file( file );

    return FFS_STATUS_ERROR;
}

int8_t ffs_i8_delete_file( ffs_file_t file_id ){

    // special case for firmware partition
    if( file_id == FFS_FILE_ID_FIRMWARE ){
        
        ffs_fw_v_erase();

        return FFS_STATUS_OK;
    }

    ASSERT( file_id < FFS_MAX_FILES );

    return ffs_page_i8_delete_file( file_id );
}

int32_t ffs_i32_read( ffs_file_t file_id, uint32_t position, void *data, uint32_t len ){
    
    // special case for firmware partition
    if( file_id == FFS_FILE_ID_FIRMWARE ){
        
        return ffs_fw_i32_read( position, data, len );
    }

    ASSERT( file_id < FFS_MAX_FILES );
    
    // get file size
    int32_t file_size = ffs_i32_get_file_size( file_id );
    ASSERT( file_size >= 0 );

    // check position is within file bounds
    if( position >= (uint32_t)file_size ){
        
        return FFS_STATUS_EOF;
    }

	int32_t total_read = 0;
    
    // start read loop
    while( len > 0 ){

        // calculate file page
        uint16_t file_page = ( position / FFS_PAGE_DATA_SIZE ) + FFS_FILE_PAGE_DATA_0;
            
        // calculate offset
        uint8_t offset = position % FFS_PAGE_DATA_SIZE;
        
        ffs_page_t page;

        // read page
        if( ffs_page_i8_read( file_id, file_page, &page ) < 0 ){
            
            return FFS_STATUS_ERROR;
        }
        
        uint8_t read_len = FFS_PAGE_DATA_SIZE;

        // bounds check on requested length
        if( (uint32_t)read_len > len ){
            
            read_len = len;
        }

        // check that the computed offset fits within the page
        // if not, the page is corrupt
        if( offset > page.len ){
            
            return FFS_STATUS_ERROR;
        }

        // bounds check on page size and offset
        if( read_len > ( page.len - offset ) ){
            
            read_len = ( page.len - offset );
        }
        
        // bounds check on end of file
        if( ( file_size - position ) < read_len ){
            
            read_len = ( file_size - position );
        }
        
        // check if any data is to be read
        if( read_len == 0 ){
            
            return FFS_STATUS_ERROR; // something went wrong, prevent infinite loop
        }

        // copy data
        memcpy( data, &page.data[offset], read_len );

        total_read  += read_len;
        data        += read_len;
        len         -= read_len;
        position    += read_len;
    }

    // check if total data read is positive (no error condition)
    if( total_read > 0 ){
        
        uint32_t stats_read;
        
        stats_read = stats_u32_read( STAT_FLASH_FS_BYTES_READ );
        stats_v_set( STAT_FLASH_FS_BYTES_READ, stats_read + total_read );
    }

	return total_read;
}

int32_t ffs_i32_write( ffs_file_t file_id, uint32_t position, const void *data, uint32_t len ){
    
    // special case for firmware partition
    if( file_id == FFS_FILE_ID_FIRMWARE ){
        
        return ffs_fw_i32_write( position, data, len );
    }

    ASSERT( file_id < FFS_MAX_FILES );
    
    // get file size
    int32_t file_size = ffs_i32_get_file_size( file_id );
    ASSERT( file_size >= 0 );

    // check position is within file bounds
    if( position > (uint32_t)file_size ){
        
        return FFS_STATUS_EOF;
    }
    
	int32_t total_written = 0;
    
    // start write loop
    while( len > 0 ){

        // calculate file page
        uint16_t file_page = ( position / FFS_PAGE_DATA_SIZE ) + FFS_FILE_PAGE_DATA_0;
            
        // calculate offset
        uint8_t offset = position % FFS_PAGE_DATA_SIZE;
        
        ffs_page_t page;
        memset( page.data, 0xff, sizeof(page.data) );
        page.len = 0;
        
        // read page
        int8_t page_read_status = ffs_page_i8_read( file_id, file_page, &page );

        // check for errors, but filter on EOF.  EOF is ok.
        if( ( page_read_status < 0 ) &&
            ( page_read_status != FFS_STATUS_EOF ) ){
            
            return FFS_STATUS_ERROR;
        }
        
        uint8_t write_len = FFS_PAGE_DATA_SIZE;

        // bounds check on requested length
        if( (uint32_t)write_len > len ){
            
            write_len = len;
        }

        // bounds check on page size and offset
        if( write_len > ( FFS_PAGE_DATA_SIZE - offset ) ){
            
            write_len = ( FFS_PAGE_DATA_SIZE - offset );
        }
        
        // copy data into page
        memcpy( &page.data[offset], data, write_len );
        
        // check if page is increasing in size
        if( page.len < ( offset + write_len ) ){

            page.len = offset + write_len;
        }

        // write page
        if( ffs_page_i8_write( file_id, file_page, &page ) < 0 ){
            
            return FFS_STATUS_ERROR;
        }

        total_written   += write_len;
        data            += write_len;
        len             -= write_len;
        position        += write_len;
    }

    // check if total data written is positive (no error condition)
    if( total_written > 0 ){
        
        uint32_t stats_written;
        
        stats_written = stats_u32_read( STAT_FLASH_FS_BYTES_WRITTEN );
        stats_v_set( STAT_FLASH_FS_BYTES_WRITTEN, stats_written + total_written );
    }

	return total_written;
}
    

