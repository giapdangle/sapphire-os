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

#include "fs.h"
#include "system.h"

#include "statistics.h"

#include <string.h>

static uint32_t stats[STAT_COUNT];


static uint16_t vfile( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
            memcpy( ptr, (void *)stats + pos, len );
            break;

        case FS_VFILE_OP_SIZE:
            len = sizeof(stats); 
            break;

        default:
            len = 0;

            break;
    }

    return len;
}

void stats_v_init( void ){
    
    // add statistics data to virtual file system
    fs_f_create_virtual( PSTR("stats"), vfile );
}

void stats_v_increment( uint8_t param ){
    
    ASSERT( param < STAT_COUNT );
    
    // prevent overflow
    if( stats[param] < 0xffffffff ){
        
        stats[param]++;
    }
}

void stats_v_set( uint8_t param, uint32_t value ){
    
    ASSERT( param < STAT_COUNT );
    
    stats[param] = value;
}

uint32_t stats_u32_read( uint8_t param ){
    
    ASSERT( param < STAT_COUNT );
    
    return stats[param];
}

const uint32_t *stats_u32p_get_all( void ){
    
    return stats;
}


