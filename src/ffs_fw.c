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

#include "ffs_global.h"
#include "flash25.h"
#include "flash_fs_partitions.h"

#include "ffs_fw.h"

static uint32_t fw_size;

int8_t ffs_fw_i8_init( void ){
    
    fw_info_t sys_fw_info;
    fw_info_t ext_fw_info;
    
    // read system firmware info
    sys_v_get_fw_info( &sys_fw_info ); 
    sys_fw_info.fw_length += sizeof(uint16_t); // adjust for CRC

    // read firmware info from external flash partition
    flash25_v_read( FLASH_FS_FIRMWARE_0_PARTITION_START + FW_INFO_ADDRESS,
                    &ext_fw_info,
                    sizeof(ext_fw_info) );
    
    fw_size = ext_fw_info.fw_length + sizeof(uint16_t); // adjust for CRC

    // bounds check
    if( fw_size > FLASH_FS_FIRMWARE_0_PARTITION_SIZE ){
        
        // invalid size, so we'll default to 0
        fw_size = 0;
    }
    
    // check CRC
    if( ffs_fw_u16_crc() != 0 ){
        
        // CRC is bad

        // erase partition
        ffs_fw_v_erase();
        
        // copy firmware to partition
        for( uint32_t i = 0; i < sys_fw_info.fw_length; i++ ){
            
            wdt_reset();
            
            // read byte from internal flash
            uint8_t temp = pgm_read_byte_far( i );

            //printf_P(PSTR("%d:%x\n"), (uint16_t)i, temp);
            
            // write to external flash
            flash25_v_write_byte( i + (uint32_t)FLASH_FS_FIRMWARE_0_PARTITION_START, temp );
        }
        
        fw_size = sys_fw_info.fw_length;

        // recheck CRC
        if( ffs_fw_u16_crc() != 0 ){
            
            fw_size = 0;

            return FFS_STATUS_ERROR;
        }
    }

    return FFS_STATUS_OK;
}

uint16_t ffs_fw_u16_crc( void ){
   
	uint16_t crc = 0xffff;
	uint32_t length = fw_size;
    uint32_t address = FLASH_FS_FIRMWARE_0_PARTITION_START;
    
	while( length > 0 ){
        
        uint8_t buf[PAGE_SIZE];
        uint16_t copy_len = PAGE_SIZE;

        if( (uint32_t)copy_len > length ){
            
            copy_len = length;
        }
        
        flash25_v_read( address, buf, copy_len );

        address += copy_len;
        length -= copy_len;

		crc = crc_u16_partial_block( crc, buf, copy_len );
		
		// reset watchdog timer
		wdt_reset();
	}

	crc = crc_u16_byte( crc, 0 );
	crc = crc_u16_byte( crc, 0 );
	
	return crc;
}

uint32_t ffs_fw_u32_size( void ){
    
    return fw_size;
}

void ffs_fw_v_erase( void ){
    
    for( uint16_t i = 0; i < FLASH_FS_FIRMWARE_0_N_BLOCKS; i++ ){
        
        // enable writes
        flash25_v_write_enable();
        
        // erase current block
        flash25_v_erase_4k( ( (uint32_t)i * (uint32_t)FLASH_FS_ERASE_BLOCK_SIZE ) + FLASH_FS_FIRMWARE_0_PARTITION_START );
        
        // wait for erase to complete
        while( flash25_b_busy() ){
            
            // kick watchdog
            wdt_reset();
        }
    }
    
    // clear firmware size
    fw_size = 0;
}

int32_t ffs_fw_i32_read( uint32_t position, void *data, uint32_t len ){

    // check position
    if( position > fw_size ){
        
        return FFS_STATUS_EOF;
    }
    
    // set read length
    uint32_t read_len = len;
    
    // bounds check and limit read len to end of file
    if( ( position + read_len ) > fw_size ){
        
        read_len = fw_size - position;
    }
    
    // copy data
    flash25_v_read( position + FLASH_FS_FIRMWARE_0_PARTITION_START, data, read_len );
    
    // return length of data copied
    return read_len;
}


int32_t ffs_fw_i32_write( uint32_t position, const void *data, uint32_t len ){
    
    // check position
    if( position > FLASH_FS_FIRMWARE_0_PARTITION_SIZE ){
        
        return FFS_STATUS_EOF;
    }
    
    // set write length
    uint32_t write_len = len;
    
    // bounds check write length to partition size
    if( ( FLASH_FS_FIRMWARE_0_PARTITION_SIZE - position ) < write_len ){
        
        write_len = FLASH_FS_FIRMWARE_0_PARTITION_SIZE - position;
    }
    
    // NOTE!!!
    // This does not check if the section being written to has been pre-erased!
    // It is up to the user to ensure that the section has been erased before
    // writing to the firmware section!
    
    // copy data
    flash25_v_write( position + FLASH_FS_FIRMWARE_0_PARTITION_START, data, write_len );
    
    // adjust file size
    fw_size = write_len + position;
    
    return write_len;
}


