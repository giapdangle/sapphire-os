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


#include "cpu.h"

#include "system.h"
#include "keyvalue.h"

#include "timers.h"
#include "threading.h"
#include "memory.h"
#include "eeprom.h"
#include "flash_fs.h"

#include <string.h>

#include "fs.h"


// KV:
static int8_t fs_i8_kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len )
{

    if( op == KV_OP_GET ){
        
        uint32_t a = 0;

        if( id == KV_ID_FREE_DISK_SPACE ){
            
            a = ffs_u32_get_free_space();
        }
        else if( id == KV_ID_TOTAL_DISK_SPACE ){
            
            a = ffs_u32_get_total_space();
        }
        else if( id == KV_ID_DISK_FILES_COUNT ){
            
            a = ffs_u32_get_file_count();
        }
        else if( id == KV_ID_MAX_DISK_FILES ){
            
            a = FLASH_FS_MAX_FILES;
        }
        else if( id == KV_ID_VIRTUAL_FILES_COUNT ){
            
            a = fs_u32_get_virtual_file_count();
        }
        else if( id == KV_ID_MAX_VIRTUAL_FILES ){
            
            a = FS_MAX_VIRTUAL_FILES;
        }

        memcpy( data, &a, sizeof(a) );
    }

    return 0;
}


KV_SECTION_META kv_meta_t fs_info_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_FREE_DISK_SPACE,     SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  0, fs_i8_kv_handler,  "fs_free_space" },
    { KV_GROUP_SYS_INFO, KV_ID_TOTAL_DISK_SPACE,    SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  0, fs_i8_kv_handler,  "fs_total_space" },
    { KV_GROUP_SYS_INFO, KV_ID_DISK_FILES_COUNT,    SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  0, fs_i8_kv_handler,  "fs_disk_files" },
    { KV_GROUP_SYS_INFO, KV_ID_MAX_DISK_FILES,      SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  0, fs_i8_kv_handler,  "fs_max_disk_files" },
    { KV_GROUP_SYS_INFO, KV_ID_VIRTUAL_FILES_COUNT, SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  0, fs_i8_kv_handler,  "fs_virtual_files" },
    { KV_GROUP_SYS_INFO, KV_ID_MAX_VIRTUAL_FILES,   SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  0, fs_i8_kv_handler,  "fs_max_virtual_files" },
};


// internal types:

typedef struct{
	file_id_t8 file_id;
	mode_t8 mode;
	uint32_t current_pos;
} file_state_t;

typedef struct{
    PGM_P filename;
    uint16_t (*handler)( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len );
} vfile_t; // virtual file

static vfile_t vfiles[FS_MAX_VIRTUAL_FILES];

void fs_v_mount( void );

static int8_t create_file_on_media( char *fname );
static uint16_t write_to_media( uint8_t file_id, uint32_t pos, const void *ptr, uint16_t len );
static uint16_t read_from_media( uint8_t file_id, uint32_t pos, void *ptr, uint16_t len );
static int8_t delete_from_media( uint8_t file_id );
static int8_t read_fname_from_media( uint8_t file_id, void *ptr, uint16_t max_len );
static uint32_t get_free_space_on_media( uint8_t file_id );
static bool media_busy( void );


static uint16_t vfile( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    // NOTE: the pos and len values are already bounds checked by the FS driver
    
    uint16_t ret_val = 0;

    if( op == FS_VFILE_OP_READ ){
        
        // iterate over data length and fill file info buffers as needed
        while( len > 0 ){
            
            uint8_t page = pos / sizeof(fs_file_info_t);
            
            // get info page
            fs_file_info_t info;
            memset( &info, 0, sizeof(info) );

            info.size = fs_i32_get_size_id( page );
            if( info.size >= 0 ){
            
                fs_i8_get_filename_id( page, info.filename, sizeof(info.filename) );
            }

            // check if virtual
            if( FS_FILE_IS_VIRTUAL( page ) ){
                
                info.flags = FS_INFO_FLAGS_VIRTUAL;
            }

            // get offset info page
            uint16_t offset = pos - ( page * sizeof(info) );
            
            // set copy length
            uint16_t copy_len = sizeof(info) - offset;

            if( copy_len > len ){
                
                copy_len = len;
            }

            // copy data
            memcpy( ptr, (void *)&info + offset, copy_len );

            // adjust pointers
            ptr += copy_len;
            len -= copy_len;
            pos += copy_len;
            ret_val += copy_len;
        }
    }
    else if( op == FS_VFILE_OP_SIZE ){
    
        ret_val = ( sizeof(fs_file_info_t) * FS_MAX_FILES );
    }

    return ret_val;
}


// User API:

// open (and/or create a file)
// returns file handle if file found or created
// returns -1 if file not found and not created
file_t fs_f_open( char filename[], mode_t8 mode ){
    
    // get file ID
    file_id_t8 file_id = fs_i8_get_file_id( filename );

    // check if exists
    if( file_id >= 0 ){

        // create file handle
        mem_handle_t handle = mem2_h_alloc( sizeof(file_state_t) );
        
        // check allocation
        if( handle < 0 ){
            
            return -1;
        }
        
        file_state_t *file_state = mem2_vp_get_ptr( handle );
        
        // set up file state
        file_state->file_id = file_id;
        file_state->mode = mode;
        
        //
        // Note that the provider of the file can ignore the mode settings
        //
        if( ( mode & FS_MODE_WRITE_APPEND ) != 0 ){

            file_state->current_pos = fs_i32_get_size_id( file_id);
        }
        else{
            
            file_state->current_pos = 0;
        }
        
        return handle;   
    }
	
	// file not found
    // check if safe mode
    // we can't create files in safe mode
    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        
        return -1;
    }
	
	// check mode
	if( ( mode & FS_MODE_CREATE_IF_NOT_FOUND ) != 0 ){
	
		// create file handle
		mem_handle_t handle = mem2_h_alloc( sizeof(file_state_t) );
		
		// check allocation
		if( handle < 0 ){
			
			return -1;
		}
		
        // create file on media
        int8_t file_id = create_file_on_media( filename );
        
        // check if file was created
        if( file_id < 0 ){
            
            // file creation failed
            mem2_v_free( handle );
            
            return -1;
        }

		// set up file state
		file_state_t *state = mem2_vp_get_ptr( handle );
		
		state->file_id = file_id;
		state->mode = mode | FS_MODE_CREATED;
		state->current_pos = 0;
		
		return handle;
	}
	
	return -1;
}

// same as fs_f_open, but with a file name from flash
file_t fs_f_open_P( PGM_P filename, mode_t8 mode ){
	
	// copy file name to memory
	char fname[FS_MAX_FILE_NAME_LEN];
	
	memset( fname, 0, sizeof(fname) );
	
	strncpy_P( fname, filename, FS_MAX_FILE_NAME_LEN );
	
	file_t file = fs_f_open( fname, mode );
	
	return file;
}

file_t fs_f_open_id( file_id_t8 file_id, uint8_t mode ){
    
    char name[FS_MAX_FILE_NAME_LEN];

    if( fs_i8_get_filename_id( file_id, name, sizeof(name) ) < 0 ){
        
        return -1;
    }
    
    return fs_f_open( name, mode );
}

file_t fs_f_create_virtual( PGM_P filename, 
                            uint16_t (*handler)( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ) ){
    
    for( uint8_t i = 0; i < FS_MAX_VIRTUAL_FILES; i++ ){
        
        if( vfiles[i].filename == 0 ){
            
            vfiles[i].filename  = filename;
            vfiles[i].handler   = handler;
            
            return i;
        }
    }
    
    return -1;
}

uint32_t fs_u32_get_virtual_file_count( void ){
    
    uint32_t count = 0;

    for( uint8_t i = 0; i < FS_MAX_VIRTUAL_FILES; i++ ){
        
        if( vfiles[i].filename != 0 ){
            
            count++;
        }
    }

    return count;
}

// returns true if the flash fs is busy.
// this only checks the flash fs - the eeprom is deprecated
// and all other media (RAM and internal flash) is read only.
bool fs_b_busy( void ){
	
	return media_busy();
}

// read from a file
// returns number of bytes read
int16_t fs_i16_read( file_t file, void *dst, uint16_t len ){
	
	// get file state
	file_state_t *state = mem2_vp_get_ptr( file );
	
    int16_t bytes_read = fs_i16_read_id( state->file_id, state->current_pos, dst, len );
    
    if( bytes_read > 0 ){

	    // increment current position
	    state->current_pos += bytes_read;
	}

	return bytes_read;
}

// read until an end of line, end of file, or maxlen characters
// note this function is not terribly efficient, it will read the entire buffer
// of maxlen data and then adjust the file pointer to the next byte after the newline.
//
// this function will handle newlines for LF (Mac, Linux) or CR+LF (Windows).
// it will also ensure null termination
int16_t fs_i16_readline( file_t file, void *dst, uint16_t maxlen ){
    
    ASSERT( maxlen > 0 ); // otherwise we crash
    
    maxlen--; // leave space at the end for a null terminator
    
    // read buffer of data from file
    uint16_t bytes_read = fs_i16_read( file, dst, maxlen );
    
    // get file state
	file_state_t *state = mem2_vp_get_ptr( file );
    
    // reset file position
    state->current_pos -= bytes_read;
    
    // find LF
    for( uint16_t i = 0; i < bytes_read; i++ ){
        
        if( ((char *)dst)[i] == 0x0A ){
            
            // reset bytes read
            bytes_read = i + 1;
            
            // check LF again
            if( ( bytes_read > 0 ) && ( ((char *)dst)[bytes_read - 1] == 0x0A ) ){
                
                // increment current position
                state->current_pos += bytes_read;
            }
            
            // set null termination
            ((char *)dst)[bytes_read] = 0;
            
            return bytes_read;
        }
    }
    
    // did not find terminator, or EOF
    return 0;
}

// write to a file
// returns number of bytes written
// note if the media driver is busy and is unable to immediately start a write,
// this function will return 0 bytes.  the application will need to retry 
// until the system is able to complete its request
int16_t fs_i16_write( file_t file, const void *src, uint16_t len ){
	
	// get file state
	file_state_t *state = mem2_vp_get_ptr( file );
	
    // check read only flag
    if( state->mode & FS_MODE_READ_ONLY ){
        
        return -1;
    }

    uint16_t bytes_written = fs_i16_write_id( state->file_id, state->current_pos, src, len );
    
    if( bytes_written > 0 ){

	    // increment current position
	    state->current_pos += bytes_written;
	}

	return bytes_written;
}

// returns a file's size
int32_t fs_i32_get_size( file_t file ){
	
	// get file state
	file_state_t *state = mem2_vp_get_ptr( file );
    
    // check if virtual
    if( FS_FILE_IS_VIRTUAL( state->file_id ) ){
        
        // compute vfile id
        uint8_t vfile_id = state->file_id - FLASH_FS_MAX_FILES;
        
        return vfiles[vfile_id].handler( FS_VFILE_OP_SIZE, 0, 0, 0 );
	}
    else{
        
        return ffs_i32_get_file_size( state->file_id );
    }
}

// get current position in a file
int32_t fs_i32_tell( file_t file ){
	
	// get file state
	file_state_t *state = mem2_vp_get_ptr( file );
	
	return state->current_pos;
}	

// set current position in a file
// if position is greater than the size of the file, it
// will be set to the end
void fs_v_seek( file_t file, uint32_t pos ){
	
	// get file state
	file_state_t *state = mem2_vp_get_ptr( file );
	
	if( (int32_t)pos > fs_i32_get_size( file ) ){
		
		pos = fs_i32_get_size( file );
	}
    
    state->current_pos = pos;
}

// delete a file
// some files cannot be deleted, this function will not report 
// whether or not the file was deleted.
// this will NOT close the file handle!
void fs_v_delete( file_t file ){
	
	// get file state
	file_state_t *state = mem2_vp_get_ptr( file );
	
    // check read only flag
    if( state->mode & FS_MODE_READ_ONLY ){
        
        return;
    }

    // check if virtual
    if( FS_FILE_IS_VIRTUAL( state->file_id ) ){

        // compute vfile id
        uint8_t vfile_id = state->file_id - FLASH_FS_MAX_FILES;
        
        vfiles[vfile_id].handler( FS_VFILE_OP_DELETE, 0, 0, 0 );
    }
    else{

        delete_from_media( state->file_id );
    }
}

// close a file
file_t fs_f_close( file_t file ){
	
    // get file state
	file_state_t *state = mem2_vp_get_ptr( file );
    
    // check if not virtual
    if( !FS_FILE_IS_VIRTUAL( state->file_id ) ){
        
        // flush the file cache
        // .... if we had one
    }
    
	mem2_v_free( file );
	
	return -1; // convience for resetting local file handle to -1
}

// initialize file system
void fs_v_init( void ){
    
    // create vfile
    fs_f_create_virtual( PSTR("fileinfo"), vfile );
}


/***************
* ID functions *
***************/

bool fs_b_exists_id( file_id_t8 id ){

    if( id >= FS_MAX_FILES ){
        
        return -1;
    }
    
    // check if virtual
    if( FS_FILE_IS_VIRTUAL( id ) ){
        
        // check if file exists
        return ( vfiles[id - FLASH_FS_MAX_FILES].filename != 0 );
    }

    return ffs_i32_get_file_size( id ) >= 0;
}

file_id_t8 fs_i8_get_file_id( char *filename ){

    // search virtual files
    for( uint8_t i = 0; i < FS_MAX_VIRTUAL_FILES; i++ ){
        
        // check if file exists at this id
        if( vfiles[i].filename != 0 ){
            
            // compare file name
            if( strncmp_P( filename, vfiles[i].filename, FS_MAX_FILE_NAME_LEN ) == 0 ){
                
                return i + FLASH_FS_MAX_FILES; // virtual files offset by FLASH_FS_MAX_FILES
            }
        }
    }
    
    // search for file on flash file system
    for( uint8_t i = 0; i < FLASH_FS_MAX_FILES; i++ ){
        
        // does file at this id exist
        if( ffs_i32_get_file_size( i ) < 0 ){
            
            continue;
        }

        char fname[FS_MAX_FILE_NAME_LEN];
        
        // read file name from media
        read_fname_from_media( i, fname, sizeof(fname) );
        
        // compare file name
        if( strncmp( fname, filename, FS_MAX_FILE_NAME_LEN ) == 0 ){
            
            return i;
        }
    }

    return -1;
}

file_id_t8 fs_i8_get_file_id_P( PGM_P filename ){

    // copy file name to memory
    char fname[FS_MAX_FILE_NAME_LEN];
    
    memset( fname, 0, sizeof(fname) );
    
    strncpy_P( fname, filename, FS_MAX_FILE_NAME_LEN );

    return fs_i8_get_file_id( fname );
}


// read a file name from given id
// return -1 if file does not exist
int8_t fs_i8_get_filename_id( file_id_t8 id, void *dst, uint16_t buf_size ){
    
    if( !fs_b_exists_id( id ) ){

        return -1;
    }

    // ensure at least all 0s are returned, in case the caller tries to print the buffer
    memset( dst, 0, buf_size );
    
    // check if virtual
    if( FS_FILE_IS_VIRTUAL( id ) ){
        
        // check if file exists
        if( vfiles[id - FLASH_FS_MAX_FILES].filename != 0 ){
            
            strncpy_P( dst, vfiles[id - FLASH_FS_MAX_FILES].filename, buf_size );
            
            return 0;
        }
        else{
            
            return -1;
        }
    }
    else{
        
        return read_fname_from_media( id, dst, buf_size );
    }
}

int32_t fs_i32_get_size_id( file_id_t8 id ){
    
    if( !fs_b_exists_id( id ) ){

        return -1;
    }
    
    // check if virtual
    if( FS_FILE_IS_VIRTUAL( id ) ){
        
        // check if file exists
        if( vfiles[id - FLASH_FS_MAX_FILES].filename != 0 ){
        
            return vfiles[id - FLASH_FS_MAX_FILES].handler( FS_VFILE_OP_SIZE, 0, 0, 0 );
        }
        else{
            
            return -1;
        }
    }
    else{
        
        return ffs_i32_get_file_size( id );
    }
}


int8_t fs_i8_delete_id( file_id_t8 id ){
            
    if( !fs_b_exists_id( id ) ){

        return -1;
    }
    
    // check if virtual
    if( FS_FILE_IS_VIRTUAL( id ) ){
        
        // can't delete virtual files
        return -1;
    }
    else{
        
        return ffs_i8_delete_file( id );
    }
}

int16_t fs_i16_read_id( file_id_t8 id, uint32_t pos, void *dst, uint16_t len ){

    if( !fs_b_exists_id( id ) ){

        return -1;
    }

    uint16_t bytes_read = 0;
    
    // check if virtual
    if( FS_FILE_IS_VIRTUAL( id ) ){
        
        // compute vfile id
        uint8_t vfile_id = id - FLASH_FS_MAX_FILES;
        
        uint32_t size = vfiles[vfile_id].handler( FS_VFILE_OP_SIZE, 0, 0, 0 ); 

        // bounds check!
        if( pos > size ){
            
            return 0;
        }
        
        uint32_t read_len = len;
        
        // bounds check!
        if( ( pos + read_len ) > size ){
        
            read_len = size - pos;
        }
        
        // read file data
        bytes_read = vfiles[vfile_id].handler( FS_VFILE_OP_READ, pos, dst, read_len );
    
    }
    else{
        
        // read file data
        bytes_read = read_from_media( id, pos, dst, len );
    }
    
    return bytes_read;
}

int16_t fs_i16_write_id( file_id_t8 id, uint32_t pos, const void *src, uint16_t len ){
        
    if( !fs_b_exists_id( id ) ){

        return -1;
    }
    
    uint16_t bytes_written = 0;
    
    // check if virtual
    if( FS_FILE_IS_VIRTUAL( id ) ){
        
        // compute vfile id
        uint8_t vfile_id = id - FLASH_FS_MAX_FILES;
       
        uint32_t size = vfiles[vfile_id].handler( FS_VFILE_OP_SIZE, 0, 0, 0 ); 

        // bounds check!
        if( pos > size ){
            
            return 0;
        }
        
        uint32_t write_len = len;
        
        // bounds check!
        if( ( pos + write_len ) > size ){
        
            write_len = size - pos;
        }
            
        // write file data
        // NOTE:
        // this discards the "const" qualifier on src.
        // we're trusting that the vfile handler is properly implemented and won't trash memory on a write
        bytes_written = vfiles[vfile_id].handler( FS_VFILE_OP_WRITE, pos, (void *)src, write_len );
    }
    else{
        
        // write file data
        bytes_written = write_to_media( id, pos, (void *)src, len );
    }
    
    return bytes_written;
}

// driver functions:

static int8_t create_file_on_media( char *fname ){
	
    int8_t file_id = ffs_i8_create_file( fname );
    
    return file_id;
}

// returns number of bytes committed to the device driver's write buffers,
// NOT the number that have actually been written to the device!  The drivers
// will stream data to the device as needed.
static uint16_t write_to_media( uint8_t file_id, uint32_t pos, const void *ptr, uint16_t len ){

    // check free space on media and limit to available space
    // this must be done even if the file size won't be changing, since the
    // underlying file system requires some free space to operate.
    uint32_t free_space = get_free_space_on_media( file_id );
    
    // bounds check requested space
    if( len > free_space ){
        
        // limit to free space available
        len = free_space;
    }
	    
    int32_t write_len;
    
    write_len = ffs_i32_write( file_id, pos, ptr, len );
    
    if( write_len < 0 ){
    
        return 0;
    }
    
    return write_len;
}

static uint16_t read_from_media( uint8_t file_id, uint32_t pos, void *ptr, uint16_t len ){
	
	// bounds check file
	if( (int32_t)( pos + len ) >= fs_i32_get_size_id( file_id ) ){
		
		len = fs_i32_get_size_id( file_id ) - pos;
	}
		
    int32_t readlen;
    
    readlen = ffs_i32_read( file_id, pos, ptr, len );
    
    if( readlen < 0 ){
    
        return 0;
    }
    
    return readlen;
}

static int8_t delete_from_media( uint8_t file_id ){

	return ffs_i8_delete_file( file_id );
}

static int8_t read_fname_from_media( uint8_t file_id, void *ptr, uint16_t max_len ){

    return ffs_i8_read_filename( file_id, ptr, max_len );
}

static uint32_t get_free_space_on_media( uint8_t file_id ){
    
    uint32_t free_space;
    
    // special handling for firmware file
    if( file_id == FFS_FILE_ID_FIRMWARE ){
        
        free_space = FLASH_FS_FIRMWARE_0_PARTITION_SIZE; 
    }
    else{
        
        free_space = ffs_u32_get_free_space();
    }

    return free_space;
}

static bool media_busy( void ){
	
	return FALSE;;
}


