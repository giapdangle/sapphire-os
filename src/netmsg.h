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

#ifndef _NETMSG_H
#define _NETMSG_H

#include "ip.h"
#include "routing2.h"
#include "memory.h"
#include "list.h"
#include "system.h"

// maximum number of messages held in ALL netmsg queues
#define NETMSG_MAX_MESSAGES     48

typedef list_node_t netmsg_t;

typedef uint8_t netmsg_flags_t;
#define NETMSG_FLAGS_WCOM_SECURITY_DISABLE  0x01
#define NETMSG_FLAGS_NO_WCOM                0x02

typedef struct{
    netmsg_flags_t  flags;
    uint8_t         data; // first byte of data
} netmsg_state_t;


// public api:
void netmsg_v_init( void );

uint8_t netmsg_u8_count( void );

netmsg_t netmsg_nm_create( void *data, uint16_t len );
void netmsg_v_release( netmsg_t netmsg );

void netmsg_v_set_flags( netmsg_t netmsg, netmsg_flags_t flags );
netmsg_flags_t netmsg_u8_get_flags( netmsg_t netmsg );

void netmsg_v_add_to_transmit_q( netmsg_t netmsg );
void netmsg_v_add_to_receive_q( netmsg_t netmsg );
netmsg_t netmsg_nm_remove_from_transmit_q( void );
netmsg_t netmsg_nm_remove_from_receive_q( void );


#ifdef ENABLE_EXTENDED_VERIFY
    void *_netmsg_vp_get_data( netmsg_t netmsg, FLASH_STRING_T file, int line );
    
    #define netmsg_vp_get_data(netmsg)         _netmsg_vp_get_data(netmsg, FLASH_STRING( __FILE__ ), __LINE__ )
#else
    void *netmsg_vp_get_data( netmsg_t netmsg );
#endif

uint16_t netmsg_u16_get_len( netmsg_t netmsg );


void netmsg_v_local_receive( netmsg_t netmsg );


#endif




