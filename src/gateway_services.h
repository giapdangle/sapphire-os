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

#ifndef _GATEWAY_SERVICES_H
#define _GATEWAY_SERVICES_H

#include "config.h"
#include "ip.h"
#include "system.h"

#define GATEWAY_SERVICES_PORT           25002
#define GATEWAY_SERVICES_UDPX_PORT      25003


#define GATEWAY_MSG_POLL_GATEWAY        1
typedef struct{
    uint8_t type;
    uint16_t short_addr;
} gate_msg_poll_t;

#define GATEWAY_MSG_GATEWAY_TOKEN       2
typedef struct{
    uint8_t type;
    uint32_t token;
    uint16_t short_addr;
	uint64_t device_id;
} gate_msg_token_t;

#define GATEWAY_MSG_REQUEST_IP_CONFIG   3
typedef struct{
    uint8_t type;
    uint8_t flags;
    uint16_t short_addr;
    ip_addr_t ip;
    uint64_t device_id;
} gate_msg_request_ip_config_t;
#define GATEWAY_MSG_REQUEST_IP_FLAGS_MANUAL_IP      0x01

#define GATEWAY_MSG_IP_CONFIG           4
typedef struct{
    uint8_t type;
    uint16_t short_addr;
    ip_addr_t ip;
    ip_addr_t subnet;
    ip_addr_t dns_server;
    ip_addr_t internet_gateway;
    uint32_t token;
} gate_msg_ip_config_t;

#define GATEWAY_MSG_REQUEST_TIME        5
typedef struct{
    uint8_t type;
} gate_msg_request_time_t;

#define GATEWAY_MSG_CURRENT_TIME        6
typedef struct{
    uint8_t type;
    uint32_t time;
} gate_msg_current_time_t;

#define GATEWAY_MSG_RESET_IP_CONFIG     7
typedef struct{
    uint8_t type;
    uint16_t short_addr;
} gate_msg_reset_ip_cfg_t;

#define GATEWAY_MSG_RESET_IP_CONFIRM    8
typedef struct{
    uint8_t type;
} gate_msg_reset_ip_confirm_t;

#define GATEWAY_MSG_GET_NETWORK_TIME    9
typedef struct{
    uint8_t type;
} gate_msg_get_network_time_t;

#define GATEWAY_MSG_NETWORK_TIME        10
typedef struct{
    uint8_t type;
    uint8_t flags;
    uint64_t ntp_time;
    uint32_t wcom_network_time;
} gate_msg_network_time_t;
#define GATEWAY_NET_TIME_FLAGS_WCOM_NETWORK_SYNC     0x01
#define GATEWAY_NET_TIME_FLAGS_NTP_SYNC              0x02
#define GATEWAY_NET_TIME_FLAGS_VALID                 0x04

void gate_svc_v_init( void );



#endif

