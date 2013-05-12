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


#ifndef _TFTP_H
#define _TFTP_H

/*! 
    Packet transfer timeout in milliseconds.
*/
#define TFTP_TIMEOUT_MS 10000

#define TFTP_SERVER_PORT 69

#define TFTP_WRITE_PACKET_FULL_LEN 516

// opcodes
#define TFTP_RRQ 1 // read request
#define TFTP_WRQ 2 // write request	
#define TFTP_DATA 3 // data
#define TFTP_ACK 4 // ack
#define TFTP_ERROR 5 // error

typedef struct{
	uint16_t opcode;
	char filename[64]; // first 64 characters of filename
} tftp_cmd_t;

typedef struct{
	uint16_t opcode;
	uint16_t block;
	uint8_t data[512];
} tftp_data_t;

typedef struct{
	uint16_t opcode;
	uint16_t block;
} tftp_ack_t;

typedef struct{
    uint16_t opcode;
    uint16_t err_code;
    char err_string[64]; // this implementation limits error message length to 63 chars + 0 terminator
} tftp_err_t;

// error codes
// this lists all codes in RFC 1350, however not all are
// used in this implementation
#define TFTP_ERR_UNDEFINED              0
#define TFTP_ERR_FILE_NOT_FOUND         1
#define TFTP_ERR_ACCESS_VIOLATION       2
#define TFTP_ERR_DISK_FULL              3
#define TFTP_ERR_ILLEGAL_OP             4
#define TFTP_ERR_UNKNOWN_XFER_ID        5
#define TFTP_ERR_FILE_ALREADY_EXISTS    6
#define TFTP_ERR_NO_SUCH_USER           7
#define TFTP_ERR_TIMED_OUT              8


void tftp_v_init( void );



#endif
