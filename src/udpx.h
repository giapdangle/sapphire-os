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

#ifndef __UDPX_H
#define __UDPX_H

#define UDPX_MAX_TRIES              5 // this is actually 4 tries
#define UDPX_INITIAL_TIMEOUT        500


typedef struct{
    uint8_t flags;
    uint8_t id;
} udpx_header_t;

#define UDPX_FLAGS_VER1         0b10000000
#define UDPX_FLAGS_VER0         0b01000000
#define UDPX_FLAGS_SVR          0b00100000
#define UDPX_FLAGS_ARQ          0b00010000
#define UDPX_FLAGS_ACK          0b00001000
#define UDPX_FLAGS_RSV2         0b00000100
#define UDPX_FLAGS_RSV1         0b00000010
#define UDPX_FLAGS_RSV0         0b00000001


netmsg_t udpx_nm_create( uint8_t msg_id,
                         uint8_t flags,
                         uint16_t source_port, 
                         uint16_t dest_port,
                         ip_addr_t dest_addr,
                         uint8_t ttl,
                         uint8_t *data,
                         uint16_t size );

#endif

