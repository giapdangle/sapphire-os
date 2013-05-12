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

#include "system.h"
#include "threading.h"
#include "config.h"
#include "timers.h"

#include "keyvalue.h"

#include "heartbeat.h"

static uint16_t heartbeat_sequence;

KV_SECTION_META kv_meta_t kv_heartbeat[] = {
    { KV_GROUP_SYS_INFO, KV_ID_HEARTBEAT,               SAPPHIRE_TYPE_UINT16, KV_FLAGS_READ_ONLY, &heartbeat_sequence, 0,  "heartbeat" },
    { KV_GROUP_SYS_CFG,  CFG_PARAM_HEARTBEAT_INTERVAL,  SAPPHIRE_TYPE_UINT16, 0, 0, cfg_i8_kv_handler,                     "heartbeat_interval" },
};


PT_THREAD( heartbeat_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;
    
    while(1){
        
        heartbeat_sequence++;
        
        kv_i8_notify( KV_GROUP_SYS_INFO, KV_ID_HEARTBEAT );
        
        // get interval from config
        uint16_t interval;
        cfg_i8_get( CFG_PARAM_HEARTBEAT_INTERVAL, &interval );
        
        // bounds check to something sane
        if( interval < 2 ){
            
            interval = 2;
        }

        timer = interval * 1000; // convert to milliseconds
        TMR_WAIT( pt, timer );
    }

PT_END( pt );
}


void hb_v_init( void ){
    
    if( cfg_i8_get( CFG_PARAM_HEARTBEAT_INTERVAL, 0 ) < 0 ){
        
        uint16_t interval = 30;

        cfg_v_set( CFG_PARAM_HEARTBEAT_INTERVAL, &interval );
    }

    thread_t_create( heartbeat_thread,
                     PSTR("heartbeat"),
                     0,
                     0 );
}

