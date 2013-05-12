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

#ifndef _NETMSG_HANDLERS_H
#define _NETMSG_HANDLERS_H

#include "at86rf230.h"
#include "wcom_mac.h"

extern int8_t ( *netmsg_i8_transmit_msg )( netmsg_t msg );
extern int8_t  ( *netmsg_i8_send_802_15_4_mac )( wcom_mac_addr_t *addr, 
                                                 wcom_mac_tx_options_t *options, 
                                                 uint16_t type,
                                                 uint8_t *data, 
                                                 uint8_t length );
extern void ( *netmsg_v_receive_msg )( netmsg_t msg );
extern void ( *netmsg_v_receive_802_15_4_raw )( rx_frame_buf_t *frame );
extern void ( *netmsg_v_receive_802_15_4_mac )( wcom_mac_addr_t *addr, 
                                                wcom_mac_rx_options_t *options, 
                                                uint8_t *data, 
                                                uint8_t len );


#endif

