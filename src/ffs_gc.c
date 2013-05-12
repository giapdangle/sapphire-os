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
#include "threading.h"
#include "system.h"
#include "fs.h"
#include "statistics.h"
#include "timers.h"

#include "flash25.h"
#include "ffs_block.h"
#include "ffs_page.h"
#include "ffs_gc.h"



PT_THREAD( garbage_collector_thread( pt_t *pt, void *state ) );
PT_THREAD( wear_leveler_thread( pt_t *pt, void *state ) );

void ffs_gc_v_init( void ){
    
    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        
        return;
    }
    
    thread_t_create( garbage_collector_thread,
                     PSTR("ffs_garbage_collector"),
                     0,
                     0 );

    
    thread_t_create( wear_leveler_thread,
                     PSTR("ffs_wear_leveler"),
                     0,
                     0 );
}


PT_THREAD( garbage_collector_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    // set up gc file
    file_t gc_file = fs_f_open_P( PSTR("gc_data"), FS_MODE_WRITE_OVERWRITE | FS_MODE_CREATE_IF_NOT_FOUND );
    
    if( gc_file >= 0 ){
        
        // check file size
        if( fs_i32_get_size( gc_file ) == 0 ){
            
            uint32_t count = 0;

            for( uint16_t i = 0; i < ffs_block_u16_total_blocks(); i++ ){
                
                fs_i16_write( gc_file, &count, sizeof(count) );
            }
        }
        
        gc_file = fs_f_close( gc_file );
    }


    while(1){

        THREAD_WAIT_WHILE( pt, ffs_block_u16_dirty_blocks() == 0 );
        
        stats_v_increment( STAT_FLASH_FS_GC_PASSES );
        
        while( ffs_block_u16_dirty_blocks() > 0 ){
            
            // get a dirty block
            block_t block = ffs_block_i16_get_dirty();
            ASSERT( block >= 0 );
            
            // erase block
            ffs_block_i8_erase( block );
            
            // open data file
            file_t f = fs_f_open_P( PSTR("gc_data"), FS_MODE_WRITE_OVERWRITE );
            
            if( f < 0 ){
                
                THREAD_YIELD( pt );

                continue;
            }

            // read erase count
            uint32_t erase_count;
            fs_v_seek( f, sizeof(erase_count) * block );
            fs_i16_read( f, &erase_count, sizeof(erase_count) );

            fs_v_seek( f, sizeof(erase_count) * block );
            erase_count++;
            fs_i16_write( f, &erase_count, sizeof(erase_count) );
            
            f = fs_f_close( f );

            THREAD_YIELD( pt );
        }
    }
	
PT_END( pt );
}


PT_THREAD( wear_leveler_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  

    static uint32_t timer;
    
    while(1){
        
        timer = 60000;
        
        TMR_WAIT( pt, timer );

        // allocate memory to hold the gc data
        mem_handle_t gc_data_h = mem2_h_alloc( sizeof(uint32_t) * ffs_block_u16_total_blocks() );
        
        if( gc_data_h < 0 ){
            
            continue;
        }

        // open the gc data file
        file_t f = fs_f_open_P( PSTR("gc_data"), FS_MODE_READ_ONLY );

        if( f < 0 ){
            
            goto done;
        }
        
        uint32_t *counts = mem2_vp_get_ptr( gc_data_h );
        
        // load list
        fs_i16_read( f, counts, mem2_u16_get_size( gc_data_h ) );
        
        // close file
        f = fs_f_close( f );
        
        uint32_t highest_erase_count = 0;
        uint32_t lowest_erase_count = 0xffffffff;
        uint16_t lowest_block = 0;
        
        // find blocks with the highest erase count and lowest erase count
        for( uint16_t i = 0; i < ffs_block_u16_total_blocks(); i++ ){
            
            if( counts[i] > highest_erase_count ){
                
                highest_erase_count = counts[i];
            }
            
            if( counts[i] < lowest_erase_count ){
                
                lowest_erase_count = counts[i];
                lowest_block = i;
            }
        }
        
        // check difference between lowest and highest
        if( ( highest_erase_count - lowest_erase_count ) > FFS_WEAR_THRESHOLD ){
            
            // check if the block is in the dirty or free lists
            if( ffs_block_b_is_block_free( lowest_block ) || ffs_block_b_is_block_dirty( lowest_block ) ){
                
                // this block is eligible for wear leveling, but since it isn't a file block,
                // there is no need to do anything with it.
                
                goto done;
            }
            
            stats_v_increment( STAT_FLASH_FS_WEAR_LEVELER_PASSES );
            
            // get meta data for lowest block
            ffs_block_meta_t meta;
            
            if( ffs_block_i8_read_meta( lowest_block, &meta ) < 0 ){
                
                goto done;
            }
            
            // replace block
            ffs_page_i16_replace_block( meta.file_id, meta.block );
        }
done:

        // release memory
        mem2_v_free( gc_data_h );
    }
	
PT_END( pt );
}

