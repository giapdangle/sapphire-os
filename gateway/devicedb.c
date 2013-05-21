
#include "cpu.h"

#include "memory.h"
#include "fs.h"

#include "devicedb.h"

//#define NO_LOGGING
#include "logging.h"


static file_t file = -1;


void devdb_v_init( void ){
    
    file = fs_f_open_P( PSTR("devicedb"), FS_MODE_WRITE_APPEND | FS_MODE_CREATE_IF_NOT_FOUND );

    if( file < 0 ){
        
        log_v_error_P( PSTR("Could not open devicedb file!") );
    }
}

void devdb_v_add_device( const devdb_device_info_t *info ){
    
    if( file < 0 ){
        
        return;
    }

    devdb_device_info_t search_dev;

    // search for existing device (by short address)
    int16_t i = devdb_i8_get_device_by_short( info->short_addr, &search_dev );
    
    if( i >= 0 ){
        
        // device exists, update
        uint32_t pos = i * sizeof(devdb_device_info_t);

        fs_v_seek( file, pos );
    }
    else{
        
        // add device to end
        fs_v_seek( file, fs_i32_tell( file ) );   
    }

    // write device info
    fs_i16_write( file, info, sizeof(devdb_device_info_t) );
}

int8_t devdb_i8_get_device_by_short( uint16_t short_addr, devdb_device_info_t *info ){
    
    for( uint16_t i = 0; i < devdb_u16_get_device_count(); i++){
        
        if( devdb_i8_get_device_by_index( i, info ) < 0 ){
            
            continue;
        }
        
        if( info->short_addr == short_addr ){
            
            return i;
        }
    }
    
    return -1;
}

int8_t devdb_i8_get_device_by_index( uint16_t index, devdb_device_info_t *info ){
    
    // check if db exists
    if( file < 0 ){
        
        return -1;
    }
    
    // calculate position
    int32_t pos = index * sizeof(devdb_device_info_t);

    // bounds check
    if( pos >= fs_i32_get_size( file ) ){
        
        return -1;
    }
    
    // seek
    fs_v_seek( file, pos );

    // read data
    fs_i16_read( file, info, sizeof(devdb_device_info_t) );

    return 0;
}

uint16_t devdb_u16_get_device_count( void ){

    // check if db exists
    if( file < 0 ){
        
        return 0;
    }
    
    return fs_i32_get_size( file ) / sizeof(devdb_device_info_t);
}


