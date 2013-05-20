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
 
#ifndef _COMMAND2_H
#define _COMMAND2_H

#include "config.h"
#include "keyvalue.h"
#include "types.h"
#include "ip.h"

#define CMD2_SERVER_PORT            16385

#define CMD2_FILE_READ_LEN          512

#define CMD2_MAX_APP_DATA_LEN       512

#define CMD2_MAX_PACKET_LEN         600

// serial port control characters
#define CMD2_SERIAL_SOF             0xfd
#define CMD2_SERIAL_ACK             0xa1
#define CMD2_SERIAL_NAK             0x1b

typedef struct{
    uint16_t len;
    uint16_t inverted_len;
} cmd2_serial_frame_header_t;

#define CMD2_SERIAL_TIMEOUT_COUNT   200000

typedef uint16_t cmd2_t16;
#define CMD2_ECHO                   1
#define CMD2_REBOOT                 2
#define CMD2_SAFE_MODE              3
#define CMD2_LOAD_FW                4

#define CMD2_FORMAT_FS              10

// DEPRECATED
#define CMD2_OPEN_FILE              11
#define CMD2_CLOSE_FILE             12
#define CMD2_READ_FILE              13
#define CMD2_WRITE_FILE             14
#define CMD2_REMOVE_FILE_OLD        15
#define CMD2_DISK_USAGE             16
#define CMD2_SEEK_FILE              17
#define CMD2_FILE_POSITION          18

#define CMD2_GET_FILE_ID            20
#define CMD2_CREATE_FILE            21
#define CMD2_READ_FILE_DATA         22
#define CMD2_WRITE_FILE_DATA        23
#define CMD2_REMOVE_FILE            24

#define CMD2_RESET_CFG              32

#define CMD2_REQUEST_ROUTE          50

#define CMD2_RESET_WCOM_TIME_SYNC   70

#define CMD2_SET_KV                 80
#define CMD2_GET_KV                 81
//#define CMD2_KV_SUBSCRIBE           82
//#define CMD2_KV_RESET_SUBS          83
//#define CMD2_KV_UNSUBSCRIBE         84

#define CMD2_SET_SECURITY_KEY       90

#define CMD2_APP_CMD_BASE           32768


typedef struct{
    cmd2_t16 cmd;
} cmd2_header_t;

typedef struct{
    uint8_t file_id;
    uint32_t pos;
    uint32_t len;
} cmd2_file_request_t;

typedef struct{
    uint8_t key_id;
    uint8_t key[CFG_KEY_SIZE];
} cmd2_set_sec_key_t;

#define CMD2_PACKET_LEN(header) ( header->len + sizeof(cmd2_header_t) )

// DEPRECATED
typedef struct{
    int8_t status;   
} cmd2_file_status_t;

extern int16_t app_i16_cmd( cmd2_t16 cmd, 
                            const void *data, 
                            uint16_t len,
                            void *response ) __attribute__((weak));

void cmd2_v_init( void );
mem_handle_t cmd2_h_process_cmd( const cmd2_header_t *cmd, int16_t len );


#endif

