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

#ifndef __KEYVALUE_H
#define __KEYVALUE_H

#include "types.h"
#include "ip.h"
#include "wcom_time.h"

#define KV_SECTION_META              __attribute__ ((section (".kv_meta")))

#define KV_INDEX_ENTRIES            8

#define KV_NAME_LEN                 32

typedef uint8_t kv_op_t8;
#define KV_OP_SET                   1
#define KV_OP_GET                   2

typedef uint8_t kv_grp_t8;
#define KV_GROUP_NULL               0
#define KV_GROUP_NULL1              254
#define KV_GROUP_SYS_CFG            1
#define KV_GROUP_SYS_INFO           2
#define KV_GROUP_SYS_STATS          3

#define KV_GROUP_APP_BASE           32


#define KV_GROUP_ALL                255

typedef uint8_t kv_id_t8;
#define KV_ID_ALL                   255

// SYS INFO
#define KV_ID_SYS_MODE                  1
#define KV_ID_BOOT_MODE                 2
#define KV_ID_VOLTAGE                   3
#define KV_ID_TEMP                      4
#define KV_ID_SYS_TIME                  5
#define KV_ID_FREE_DISK_SPACE           6
#define KV_ID_TOTAL_DISK_SPACE          7  
#define KV_ID_DISK_FILES_COUNT          8
#define KV_ID_MAX_DISK_FILES            9
#define KV_ID_VIRTUAL_FILES_COUNT       10
#define KV_ID_MAX_VIRTUAL_FILES         11
#define KV_ID_THREAD_COUNT              12
#define KV_ID_PEAK_THREAD_COUNT         13
#define KV_ID_THREAD_TASK_TIME          14
#define KV_ID_THREAD_SLEEP_TIME         15
#define KV_ID_THREAD_LOOPS              16
#define KV_ID_LOADER_VERSION_MINOR      17
#define KV_ID_LOADER_VERSION_MAJOR      18
#define KV_ID_LOADER_STATUS             19
#define KV_ID_MEM_HANDLES               20
#define KV_ID_MAX_MEM_HANDLES           21
#define KV_ID_MEM_STACK                 22
#define KV_ID_MAX_MEM_STACK             23
#define KV_ID_MEM_HEAP_SIZE             24
#define KV_ID_MEM_FREE                  25
#define KV_ID_MEM_PEAK                  26
#define KV_ID_WCOM_MAC_BE               27
#define KV_ID_WCOM_NEI_UPSTREAM         28
#define KV_ID_WCOM_NEI_DEPTH            29
#define KV_ID_WCOM_NEI_BEACON_INTERVAL  30
#define KV_ID_WCOM_TIME_FLAGS           31
#define KV_ID_WCOM_TIME_SOURCE_ADDR     32
#define KV_ID_WCOM_TIME_CLOCK_SOURCE    33
#define KV_ID_WCOM_TIME_DEPTH           34
#define KV_ID_WCOM_TIME_SEQUENCE        35
#define KV_ID_WCOM_TIME_DRIFT           36
#define KV_ID_WCOM_TIME_LOCAL           37
#define KV_ID_WCOM_TIME_NETWORK         38
#define KV_ID_SYS_WARNINGS              39
#define KV_ID_THREAD_RUN_TIME           40
#define KV_ID_NTP_SECONDS               41
#define KV_ID_HEARTBEAT                 99


typedef uint16_t kv_flags_t16;
#define KV_FLAGS_READ_ONLY          0x0001
#define KV_FLAGS_PERSIST            0x0004


// Error codes
#define KV_ERR_STATUS_OK                        0
#define KV_ERR_STATUS_NOT_FOUND                 -1
#define KV_ERR_STATUS_READONLY                  -2
#define KV_ERR_STATUS_INVALID_TYPE              -3
#define KV_ERR_STATUS_OUTPUT_BUF_TOO_SMALL      -4
#define KV_ERR_STATUS_INPUT_BUF_TOO_SMALL       -5
#define KV_ERR_STATUS_TYPE_MISMATCH             -6
#define KV_ERR_STATUS_SAFE_MODE                 -7
#define KV_ERR_STATUS_PARAMETER_NOT_SET         -8
/*
typedef struct{
    kv_grp_t8 group;
    kv_id_t8 id;
    ip_addr_t ip;
    uint16_t port;
} kv_subscription_t;
*/
typedef int8_t ( *kv_handler_t )( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len );

typedef struct{
    kv_grp_t8 group;
    kv_id_t8 id;
    sapphire_type_t8 type;
    kv_flags_t16 flags;
    void *ptr;
    kv_handler_t handler;
    char name[KV_NAME_LEN];
} kv_meta_t;

typedef struct{
    kv_grp_t8 group;
    kv_id_t8 id;
    sapphire_type_t8 type;
} kv_param_t;

typedef struct{
    kv_grp_t8 group;
    kv_id_t8 id;
    sapphire_type_t8 status;
} kv_param_status_t;


// Messages:

#define KV_MSG_TYPE_NOTIFICATION_0      1

typedef struct{
    uint8_t msg_type;
    uint8_t flags;
    uint64_t device_id;
    ntp_ts_t timestamp;
    kv_grp_t8 group;
    kv_id_t8 id;
    sapphire_type_t8 data_type;
    // data follows
} kv_msg_notification_t;
#define KV_MSG_FLAGS_TIMESTAMP_VALID    0x01



// prototypes:

void kv_v_init( void );

int8_t kv_i8_set( 
    kv_grp_t8 group,
    kv_id_t8 id,
    const void *data,
    uint16_t len );

int8_t kv_i8_get( 
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t max_len );

int16_t kv_i16_len(
    kv_grp_t8 group,
    kv_id_t8 id );

sapphire_type_t8 kv_i8_type(
    kv_grp_t8 group,
    kv_id_t8 id );

int16_t kv_i16_batch_set( 
    const void *input, 
    int16_t input_len,
    void *output,
    int16_t max_output_len );

int16_t kv_i16_batch_get( 
    const void *input, 
    int16_t input_len,
    void *output,
    int16_t max_output_len );

int8_t kv_i8_persist( 
    kv_grp_t8 group,
    kv_id_t8 id );

int8_t kv_i8_notify( 
    kv_grp_t8 group,
    kv_id_t8 id );

void kv_v_set_server(
    ip_addr_t ip,
    uint16_t port );

/*
int8_t kv_i8_subscribe(
    kv_grp_t8 group,
    kv_id_t8 id,
    ip_addr_t ip,
    uint16_t port );

int8_t kv_i8_unsubscribe(
    kv_grp_t8 group,
    kv_id_t8 id,
    ip_addr_t ip,
    uint16_t port );

void kv_v_reset_subscriptions( void );
*/
#endif



