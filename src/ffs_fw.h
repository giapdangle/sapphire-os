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

#ifndef _FFS_FW_H
#define _FFS_FW_H


int8_t ffs_fw_i8_init( void );

uint16_t ffs_fw_u16_crc( void );
uint32_t ffs_fw_u32_size( void );
void ffs_fw_v_erase( void );
int32_t ffs_fw_i32_read( uint32_t position, void *data, uint32_t len );
int32_t ffs_fw_i32_write( uint32_t position, const void *data, uint32_t len );


#endif

