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
#include "config.h"
#include "types.h"
#include "fs.h"
#include "list.h"
#include "wcom_time.h"
#include "sockets.h"

//#define NO_LOGGING
#include "logging.h"

#include "keyvalue.h"


#define KV_SECTION_META_START       __attribute__ ((section (".kv_meta_start")))
#define KV_SECTION_META_END         __attribute__ ((section (".kv_meta_end")))

KV_SECTION_META_START kv_meta_t kv_start[] = {
    { KV_GROUP_NULL, 0, SAPPHIRE_TYPE_NONE, 0, 0, 0, "kvstart" }
};

KV_SECTION_META_END kv_meta_t kv_end[] = {
    { KV_GROUP_NULL1, 1, SAPPHIRE_TYPE_NONE, 0, 0, 0, "kvend" }
};

KV_SECTION_META kv_meta_t kv_cfg[] = {
    { KV_GROUP_SYS_CFG, CFG_PARAM_MAX_KV_NOTIFICATIONS, SAPPHIRE_TYPE_UINT16, 0, 0, cfg_i8_kv_handler,  "max_kv_notifications" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_MAX_KV_SUBSCRIPTIONS, SAPPHIRE_TYPE_UINT16, 0, 0, cfg_i8_kv_handler,  "max_kv_subscriptions" },
};

static list_t subscription_list;
static list_t notification_list;

typedef struct{;
    kv_grp_t8 group;
    kv_id_t8 id;
    kv_meta_t *ptr;
} kv_index_t;

static kv_index_t kv_index[KV_INDEX_ENTRIES];
static uint8_t kv_index_insert;

typedef struct{
    kv_grp_t8 group;
    kv_id_t8 id;
    sapphire_type_t8 type;
} kv_persist_block_header_t;
#define KV_PERSIST_BLOCK_DATA_LEN   SAPPHIRE_TYPE_MAX_LEN


PT_THREAD( notifications_processor_thread( pt_t *pt, void *state ) );

static uint16_t kv_meta_vfile_handler( 
    vfile_op_t8 op, 
    uint32_t pos, 
    void *ptr, 
    uint16_t len )
{
    
    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
            memcpy_P( ptr, (void *)kv_start + sizeof(kv_meta_t) + pos, len );
            break;

        case FS_VFILE_OP_SIZE:
            len = ( (void *)kv_end - (void *)kv_start ) - sizeof(kv_meta_t);
            break;

        case FS_VFILE_OP_DELETE:
            kv_v_reset_subscriptions();
            break;

        default:
            len = 0;
            break;
    }

    return len;
}

static uint16_t kv_sub_vfile_handler( 
    vfile_op_t8 op, 
    uint32_t pos, 
    void *ptr, 
    uint16_t len )
{
    
    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
            list_u16_flatten( &subscription_list, pos, ptr, len );
            break;

        case FS_VFILE_OP_SIZE:
            len = list_u16_size( &subscription_list );
            break;

        default:
            len = 0;
            break;
    }

    return len;
}


static int8_t kv_i8_lookup_meta( 
    kv_grp_t8 group, 
    kv_id_t8 id, 
    kv_meta_t *meta )
{
    // search index
    for( uint8_t i = 0; i < KV_INDEX_ENTRIES; i++ ){
        
        if( ( kv_index[i].group == group ) &&
            ( kv_index[i].id == id ) ){
            
            // load meta data
            // NOTE: we're skipping reading the name itself since we
            // don't need it and it adds up to a lot of wasted cycles
            memcpy_P( meta, kv_index[i].ptr, sizeof(kv_meta_t) - KV_NAME_LEN );

            // parameter found
            return KV_ERR_STATUS_OK;
        }
    }

    kv_meta_t *ptr = (kv_meta_t *)kv_start;

    // iterate through handlers to find a match
    while( ptr < kv_end ){
        
        // load meta data
        // NOTE: we're skipping reading the name itself since we
        // don't need it and it adds up to a lot of wasted cycles
        memcpy_P( meta, ptr, sizeof(kv_meta_t) - KV_NAME_LEN );
        
        // check group and id
        if( ( meta->group == group ) && ( meta->id == id ) ){
            
            // check type
            if( type_u16_size( meta->type ) == SAPPHIRE_TYPE_INVALID ){
                
                // invalid type
                return KV_ERR_STATUS_INVALID_TYPE;
            }
            
            // add to index
            kv_index[kv_index_insert].group = group;
            kv_index[kv_index_insert].id    = id;
            kv_index[kv_index_insert].ptr   = ptr;
        
            kv_index_insert++;

            if( kv_index_insert >= KV_INDEX_ENTRIES ){
                
                kv_index_insert = 0;
            }

            // parameter found
            return KV_ERR_STATUS_OK;
        }

        ptr++;
    }

    // no handler for this group
    return KV_ERR_STATUS_NOT_FOUND;
}

static int8_t kv_i8_init_persist( void ){
    
    file_t f = fs_f_open_P( PSTR("kv_data"), FS_MODE_READ_ONLY );
    
    if( f < 0 ){

        return -1;
    }

    uint8_t data[sizeof(kv_persist_block_header_t) + KV_PERSIST_BLOCK_DATA_LEN];
    kv_persist_block_header_t *hdr = (kv_persist_block_header_t *)data;
    kv_meta_t meta;

    while( fs_i16_read( f, data, sizeof(data) ) == sizeof(data) ){

        // look up meta data, verify type matches, and check if there is 
        // a memory pointer
        if( ( kv_i8_lookup_meta( hdr->group, hdr->id, &meta ) >= 0 ) &&
            ( meta.type == hdr->type ) &&
            ( meta.ptr != 0 ) ){

            memcpy( meta.ptr, data + sizeof(kv_persist_block_header_t), type_u16_size( meta.type ) );
        }
    }

    fs_f_close( f );

    return 0;
}

void kv_v_init( void ){

    // clear index
    memset( kv_index, 0xff, sizeof(kv_index) );

    list_v_init( &subscription_list );
    list_v_init( &notification_list );

    fs_f_create_virtual( PSTR("kvmeta"), kv_meta_vfile_handler );
    fs_f_create_virtual( PSTR("kvsubs"), kv_sub_vfile_handler );
    
    // check if safe mode
    if( sys_u8_get_mode() != SYS_MODE_SAFE ){

        thread_t_create( notifications_processor_thread,
                         PSTR("notifications_processor"),
                         0,
                         0 );
        
        // initialize all persisted KV items
        kv_i8_init_persist();
    }
}


static int8_t kv_i8_persist_set(
    kv_meta_t *meta,
    const void *data,
    uint16_t len )
{
    file_t f = fs_f_open_P( PSTR("kv_data"), FS_MODE_WRITE_OVERWRITE | FS_MODE_CREATE_IF_NOT_FOUND );
    
    if( f < 0 ){

        return -1;
    }   

    kv_persist_block_header_t hdr;

    // seek to matching item or end of file
    while( fs_i16_read( f, &hdr, sizeof(hdr) ) == sizeof(hdr) ){

        if( ( hdr.group == meta->group ) && ( hdr.id == meta->id ) ){

            // back up the file position
            fs_v_seek( f, fs_i32_tell( f ) - sizeof(hdr) );

            break;
        }

        // advance position to next entry
        fs_v_seek( f, fs_i32_tell( f ) + KV_PERSIST_BLOCK_DATA_LEN );
    }

    // set up header
    hdr.group = meta->group;
    hdr.id = meta->id;
    hdr.type = meta->type;

    // write header
    fs_i16_write( f, &hdr, sizeof(hdr) );

    // bounds check data
    if( len > KV_PERSIST_BLOCK_DATA_LEN ){

        len = KV_PERSIST_BLOCK_DATA_LEN;
    }

    // Copy data to 0 initialized buffer of the full data length.
    // Even if the value being persisted is smaller than this, we
    // write the full block so that the file always has an even
    // number of full blocks.  Otherwise, the (somewhat naive) scanning
    // algorithm will miss the last block.
    uint8_t buf[KV_PERSIST_BLOCK_DATA_LEN];
    memset( buf, 0, sizeof(buf) );

    memcpy( buf, data, len );

    // write data
    fs_i16_write( f, buf, sizeof(buf) );

    fs_f_close( f );

    return 0;
}

static int8_t kv_i8_internal_set( 
    kv_meta_t *meta,
    const void *data,
    uint16_t len )
{

    // check group and safe mode
    if( ( sys_u8_get_mode() == SYS_MODE_SAFE ) &&
        ( meta->group >= KV_GROUP_APP_BASE ) ){
        
        return KV_ERR_STATUS_SAFE_MODE;
    }
    
    // check flags for readonly
    if( meta->flags & KV_FLAGS_READ_ONLY ){
        
        return KV_ERR_STATUS_READONLY;
    }

    // set copy length
    uint16_t copy_len = type_u16_size( meta->type );

    if( copy_len > len ){
        
        copy_len = len;
    }

    // check if persist flag is set
    if( meta->flags & KV_FLAGS_PERSIST ){

        kv_i8_persist_set( meta, data, copy_len );
    }

    // check if parameter has a pointer
    if( meta->ptr != 0 ){
        
        ATOMIC;

        // set data
        memcpy( meta->ptr, data, copy_len );

        END_ATOMIC;
    }

    // check if parameter has a notifier
    if( meta->handler == 0 ){
       
        return KV_ERR_STATUS_OK;
    }

    ATOMIC;

    // call handler
    int8_t status = meta->handler( KV_OP_SET, meta->group, meta->id, (void *)data, copy_len );
    
    END_ATOMIC;

    return status;
}

int8_t kv_i8_set( 
    kv_grp_t8 group,
    kv_id_t8 id,
    const void *data,
    uint16_t len )
{   
    // look up parameter
    kv_meta_t meta;

    int8_t status = kv_i8_lookup_meta( group, id, &meta );

    if( status < 0 ){
        
        return status;
    }

    return kv_i8_internal_set( &meta, data, len );
}

static int8_t kv_i8_persist_get(
    kv_meta_t *meta,
    void *data,
    uint16_t len )
{
    file_t f = fs_f_open_P( PSTR("kv_data"), FS_MODE_READ_ONLY );
    
    if( f < 0 ){

        return -1;
    }   

    kv_persist_block_header_t hdr;
    memset( &hdr, 0, sizeof(hdr) ); // init to all 0s in case the file is empty

    // seek to matching item or end of file
    while( fs_i16_read( f, &hdr, sizeof(hdr) ) == sizeof(hdr) ){

        if( ( hdr.group == meta->group ) && ( hdr.id == meta->id ) ){

            // back up the file position
            fs_v_seek( f, fs_i32_tell( f ) - sizeof(hdr) );

            break;
        }

        // advance position to next entry
        fs_v_seek( f, fs_i32_tell( f ) + KV_PERSIST_BLOCK_DATA_LEN );
    }

    uint16_t data_read = 0;

    // check if data was found
    if( ( hdr.group == meta->group ) && ( hdr.id == meta->id ) ){

        data_read = fs_i16_read( f, data, len );
    }

    fs_f_close( f );

    // check if correct amount of data was read.
    // data_read will be 0 if the item was not found in the file.
    if( data_read != len ){

        return -1;
    }

    return 0;
}

static int8_t kv_i8_internal_get( 
    kv_meta_t *meta,
    void *data,
    uint16_t max_len )
{
    
    // check group and safe mode
    if( ( sys_u8_get_mode() == SYS_MODE_SAFE ) &&
        ( meta->group >= KV_GROUP_APP_BASE ) ){
        
        return KV_ERR_STATUS_SAFE_MODE;
    }

    uint16_t type_size = type_u16_size( meta->type );

    // check if max buffer length has enough space for data type
    if( max_len < type_size ){
        
        return KV_ERR_STATUS_OUTPUT_BUF_TOO_SMALL;
    }

    // set copy length
    uint16_t copy_len = type_size;

    if( copy_len > max_len ){
        
        copy_len = max_len;
    }

    // check if persist flag is set
    if( meta->flags & KV_FLAGS_PERSIST ){

        // check data from file system
        if( kv_i8_persist_get( meta, data, max_len ) < 0 ){

            // did not return any data, set default
            memset( data, 0, max_len );
        }        
    }

    // check if parameter has a pointer
    if( meta->ptr != 0 ){
        
        ATOMIC;

        // get data
        memcpy( data, meta->ptr, copy_len );

        END_ATOMIC;
    }

    // check if parameter has a notifier
    if( meta->handler == 0 ){
       
        return KV_ERR_STATUS_OK;
    }
    
    ATOMIC;

    // call handler
    int8_t status = meta->handler( KV_OP_GET, meta->group, meta->id, data, copy_len );

    END_ATOMIC;

    return status;
}

int8_t kv_i8_get( 
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t max_len )
{

    // look up parameter
    kv_meta_t meta;

    int8_t status = kv_i8_lookup_meta( group, id, &meta );

    if( status < 0 ){
        
        return status;
    }

    return kv_i8_internal_get( &meta, data, max_len );
}

int16_t kv_i16_len(
    kv_grp_t8 group,
    kv_id_t8 id )
{   
    kv_meta_t meta;
    
    int8_t status = kv_i8_lookup_meta( group, id, &meta );

    if( status < 0 ){
        
        return status;
    }

    return type_u16_size( meta.type );
}

sapphire_type_t8 kv_i8_type(
    kv_grp_t8 group,
    kv_id_t8 id )
{

    kv_meta_t meta;
    
    int8_t status = kv_i8_lookup_meta( group, id, &meta );

    if( status < 0 ){
        
        return status;
    }

    return meta.type;
}

int16_t kv_i16_batch_set( 
    const void *input, 
    int16_t input_len,
    void *output,
    int16_t max_output_len )
{
    uint16_t output_len = 0;

    // loop through parameters
    while( input_len > 0 ){
        
        // map status pointer
        kv_param_status_t *status = (kv_param_status_t *)output;
        
        // advance output pointer
        output          += sizeof(kv_param_status_t);
        max_output_len  -= sizeof(kv_param_status_t);
        
        // check if the output buffer had enough space
        if( max_output_len < 0 ){
            
            log_v_warn_P( PSTR("KV batch output buffer too small") );

            // exit loop and return whatever data we got
            break;
        }
    
        // map param pointer
        const kv_param_t *param = (const kv_param_t *)input;
    
        // advance input pointer
        input           += sizeof(kv_param_t);
        input_len       -= sizeof(kv_param_t);

        // check input buffer length
        if( input_len < 0 ){
            
            log_v_warn_P( PSTR("KV batch input buffer too small") );
            //return KV_ERR_STATUS_INPUT_BUF_TOO_SMALL;
            break;
        }

        // advance output length
        output_len += sizeof(kv_param_status_t);

        kv_meta_t meta;
        
        // get meta data for parameter
        status->group   = param->group;
        status->id      = param->id;
        status->status  = kv_i8_lookup_meta( param->group, param->id, &meta );
                    
        uint16_t param_len = type_u16_size( param->type );
        
        // check param length
        if( param_len == SAPPHIRE_TYPE_INVALID ){
            
            // bail out on invalid types since we can't
            // parse the data structure
            log_v_warn_P( PSTR("KV batch input invalid type") );
            //return KV_ERR_STATUS_INVALID_TYPE;
            break;
        }
        
        // get var data pointer
        uint8_t *var_data = (uint8_t *)input;

        // advance input pointer again (for var data this time)
        input           += param_len;
        input_len       -= param_len;
        
        // check input buffer length again
        if( input_len < 0 ){
            
            log_v_warn_P( PSTR("KV batch input buffer too small") );
            //return KV_ERR_STATUS_INPUT_BUF_TOO_SMALL;
            break;
        }

        // check if parameter found
        if( status->status >= 0 ){
        
            // check if types match
            if( param->type == meta.type ){
                
                // set status to type
                status->status = meta.type;

                // set parameter
                kv_i8_internal_set( &meta, var_data, param_len );
            }
            else{
                
                status->status = KV_ERR_STATUS_TYPE_MISMATCH;
            }
        }
    }
    
    // return number of bytes in output buffer
    return output_len;
}

int16_t kv_i16_batch_get( 
    const void *input, 
    int16_t input_len,
    void *output,
    int16_t max_output_len )
{

    uint16_t output_len = 0;

    // check for output buffer size to be somewhat sensible
    if( max_output_len < (int16_t)( sizeof(uint8_t) ) ){

        log_v_warn_P( PSTR("KV batch output buffer too small") );

        return KV_ERR_STATUS_OUTPUT_BUF_TOO_SMALL;
    }
    
    // loop through parameters
    while( input_len > 0 ){
        
        // map status pointer
        kv_param_status_t *status = (kv_param_status_t *)output;
        
        // advance output pointer
        output          += sizeof(kv_param_status_t);
        max_output_len  -= sizeof(kv_param_status_t);
        
        // check if the output buffer had enough space
        if( max_output_len < 0 ){
            
            log_v_warn_P( PSTR("KV batch output buffer too small") );

            // exit loop and return whatever data we got
            break;
        }

        // map param pointer
        const kv_param_t *param = (const kv_param_t *)input;
    
        // advance input pointer
        input           += sizeof(kv_param_t);
        input_len       -= sizeof(kv_param_t);
        
        // check input buffer length
        if( input_len < 0 ){
            
            log_v_warn_P( PSTR("KV batch input buffer too small") );
            //return KV_ERR_STATUS_INPUT_BUF_TOO_SMALL;
            break;
        }

        output_len += sizeof(kv_param_status_t);

        kv_meta_t meta;
        
        // get meta data for parameter
        status->group   = param->group;
        status->id      = param->id;
        status->status  = kv_i8_lookup_meta( param->group, param->id, &meta );
                    
        uint16_t param_len = type_u16_size( param->type );
        
        // check param length
        if( param_len == SAPPHIRE_TYPE_INVALID ){
            
            // bail out on invalid types since we can't
            // parse the data structure
            log_v_warn_P( PSTR("KV batch input invalid type") );
            //return KV_ERR_STATUS_INVALID_TYPE;
            break;
        }
        
        // check if parameter found
        if( status->status >= 0 ){
        
            // check if types match
            if( param->type == meta.type ){

                // get var data pointer
                uint8_t *var_data = (uint8_t *)output;

                // advance input pointer again (for var data this time)
                output          += param_len;
                max_output_len  -= param_len;
                
                // check output buffer length again
                if( max_output_len < 0 ){
                    
                    log_v_warn_P( PSTR("KV batch output buffer too small") );

                    // exit loop and return whatever data we got
                    break;
                }
                
                // set status to type
                status->status = meta.type;

                // get parameter
                kv_i8_internal_get( &meta, var_data, param_len );

                output_len += param_len;
            }
            else{
                
                status->status = KV_ERR_STATUS_TYPE_MISMATCH;
            }
        }
    }
    
    // return number of bytes in output buffer
    return output_len;
}

static void kv_push_notification( 
    kv_meta_t *meta, 
    ntp_ts_t timestamp, 
    uint8_t flags,
    void *data,
    uint16_t len )
{
    // check if safe mode
    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        
        return;
    }

    // get max notifications
    uint16_t max_notifications;
    cfg_i8_get( CFG_PARAM_MAX_KV_NOTIFICATIONS, &max_notifications );

    // check current Q size
    if( list_u8_count( &notification_list ) >= max_notifications ){
        
        log_v_warn_P( PSTR("Notification Q full") );

        return;
    }

    // create new event
    list_node_t ln = list_ln_create_node( 0, sizeof(kv_msg_notification_t) + len );
    
    // check creation
    if( ln < 0 ){
        
        return;
    }
    
    // get pointer and copy data
    kv_msg_notification_t *msg = list_vp_get_data( ln );
    
    msg->msg_type       = KV_MSG_TYPE_NOTIFICATION_0;
    msg->flags          = flags;
    msg->timestamp      = timestamp;
    msg->group          = meta->group;
    msg->id             = meta->id;
    msg->data_type      = meta->type;
    cfg_i8_get( CFG_PARAM_DEVICE_ID, &msg->device_id );
    
    void *data_dest = (void *)( msg + 1 );

    memcpy( data_dest, data, len );
    
    // append to queue
    list_v_insert_head( &notification_list, ln );
}


int8_t kv_i8_persist( 
    kv_grp_t8 group,
    kv_id_t8 id )
{
    // look up parameter
    kv_meta_t meta;

    int8_t status = kv_i8_lookup_meta( group, id, &meta );

    if( status < 0 ){
        
        // parameter not found
        log_v_error_P( PSTR("KV param not found") );
        
        return status;
    }
    
    // check for persist flag
    if( ( meta.flags & KV_FLAGS_PERSIST ) == 0 ){
        
        log_v_error_P( PSTR("Persist flag not set!") );
        
        return -1;
    }
    
    // get parameter data
    uint8_t data[SAPPHIRE_TYPE_MAX_LEN];
    kv_i8_internal_get( &meta, data, sizeof(data) );
    
    // get parameter length
    uint16_t param_len = type_u16_size( meta.type );

    return kv_i8_persist_set( &meta, data, param_len );
}
    

int8_t kv_i8_notify( 
    kv_grp_t8 group,
    kv_id_t8 id )
{
     
    // record timestamp
    ntp_ts_t timestamp = wcom_time_t_get_ntp_time();
    
    // check timestamp validity
    uint8_t flags = 0;
    
    if( wcom_time_b_sync() ){
        
        flags |= KV_MSG_FLAGS_TIMESTAMP_VALID;
    }

    uint8_t data[SAPPHIRE_TYPE_MAX_LEN];
    
    // look up parameter
    kv_meta_t meta;

    int8_t status = kv_i8_lookup_meta( group, id, &meta );

    if( status < 0 ){
        
        // parameter not found
        log_v_error_P( PSTR("KV param not found") );
        
        return status;
    }

    // get parameter data
    kv_i8_internal_get( &meta, data, sizeof(data) );
    
    // get parameter length
    uint16_t param_len = type_u16_size( meta.type );

    // push data to notification processor
    kv_push_notification( &meta, 
                          timestamp, 
                          flags,
                          data,
                          param_len );

    return KV_ERR_STATUS_OK;
}

int8_t kv_i8_subscribe(
    kv_grp_t8 group,
    kv_id_t8 id,
    ip_addr_t ip,
    uint16_t port )
{

    // check if safe mode
    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        
        return KV_ERR_STATUS_SAFE_MODE;
    }
    
    list_node_t ln;

    // get max subscribers from config
    uint16_t max_subscribers;
    cfg_i8_get( CFG_PARAM_MAX_KV_SUBSCRIPTIONS, &max_subscribers );

    // check subscriber list size
    if( list_u8_count( &subscription_list ) >= max_subscribers ){
        
        // throw away the oldest
        ln = list_ln_remove_tail( &subscription_list );

        if( ln >= 0 ){
            
            list_v_release_node( ln );
        }
    }
    
    // check if we already have this subscription
    ln = subscription_list.head;
    
    while( ln >= 0 ){
        
        kv_subscription_t *sub = list_vp_get_data( ln );
        
        if( ( sub->group == group ) &&
            ( sub->id == id ) &&
            ( ip_b_addr_compare( sub->ip, ip ) ) &&
            ( sub->port == port ) ){
            
            return KV_ERR_STATUS_OK;
        }

        ln = list_ln_next( ln );
    }
    
    // create subscription
    kv_subscription_t sub;

    sub.group   = group;
    sub.id      = id;
    sub.ip      = ip;
    sub.port    = port;
    
    ln = list_ln_create_node( &sub, sizeof(sub) );
    
    if( ln >= 0 ){
        
        list_v_insert_head( &subscription_list, ln );
    }
    
    return KV_ERR_STATUS_OK;
}


int8_t kv_i8_unsubscribe(
    kv_grp_t8 group,
    kv_id_t8 id,
    ip_addr_t ip,
    uint16_t port )
{

    list_node_t ln;

    // look for subscription
    ln = subscription_list.head;
    
    while( ln >= 0 ){
        
        kv_subscription_t *sub = list_vp_get_data( ln );
        
        if( ( sub->group == group ) &&
            ( sub->id == id ) &&
            ( ip_b_addr_compare( sub->ip, ip ) ) &&
            ( sub->port == port ) ){
            
            // remove subscription
            list_v_remove( &subscription_list, ln );
            list_v_release_node( ln );

            return KV_ERR_STATUS_OK;
        }

        ln = list_ln_next( ln );
    }
    
    return KV_ERR_STATUS_OK;
}

void kv_v_reset_subscriptions( void ){
    
    list_v_destroy( &subscription_list );
}


typedef struct{
    socket_t sock;
    ip_addr_t target_ip;
    uint16_t target_port;
    uint16_t data_len;
    // data follows
    uint8_t data; // first byte
} sender_state_t;

PT_THREAD( notification_sender_thread( pt_t *pt, sender_state_t *state ) )
{
PT_BEGIN( pt );  
    
    // create socket
    state->sock = sock_s_create( SOCK_UDPX_CLIENT );
    
    // check if socket was created
    if( state->sock < 0 ){
        
        THREAD_EXIT( pt );
    }

    // set target address
    sock_addr_t raddr;
    raddr.ipaddr = state->target_ip;
    raddr.port   = state->target_port;
    
    // transmit data
    sock_i16_sendto( state->sock, 
                     &state->data,
                     state->data_len,
                     &raddr );
    
    // wait for response or timeout
    THREAD_WAIT_WHILE( pt, sock_i8_recvfrom( state->sock ) < 0 );
    
    // release socket
    sock_v_release( state->sock );

PT_END( pt );
}

PT_THREAD( notifications_processor_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while(1){
        
        // prevent runaway thread
        THREAD_YIELD( pt );

        // wait for notifications
        THREAD_WAIT_WHILE( pt, list_u8_count( &notification_list ) == 0 );
        
        // remove list item
        list_node_t notif_ln = list_ln_remove_tail( &notification_list );
        
        ASSERT( notif_ln >= 0 );

        // get event data
        kv_msg_notification_t *notif = list_vp_get_data( notif_ln );
        
        // iterate through subscriptions
        list_node_t sub_ln = subscription_list.head;
        
        while( sub_ln >= 0 ){
            
            // get state for current list node
            kv_subscription_t *sub = list_vp_get_data( sub_ln );
            
            // check if subscription matches data
            if( ( sub->group == KV_GROUP_ALL ) ||
                ( ( sub->group == notif->group ) && 
                  ( ( sub->id == notif->id ) ||
                    ( sub->id == KV_ID_ALL ) ) ) ){
                
                thread_t sender_thread = 
                    thread_t_create( THREAD_CAST(notification_sender_thread),
                                     PSTR("notification_sender"),
                                     0,
                                     sizeof(sender_state_t) + list_u16_node_size( notif_ln ) );
                
                // check if thread was created
                if( sender_thread < 0 ){
                    
                    break; // exit loop, since we can't create any more threads
                }

                // get thread state
                sender_state_t *sender_state = thread_vp_get_data( sender_thread );
                
                sender_state->target_ip     = sub->ip;
                sender_state->target_port   = sub->port;
                sender_state->data_len      = list_u16_node_size( notif_ln );
                memcpy( &sender_state->data, notif, list_u16_node_size( notif_ln ) );

            }
            
            sub_ln = list_ln_next( sub_ln );
        }

        // release notification
        list_v_release_node( notif_ln );
    }

PT_END( pt );
}


