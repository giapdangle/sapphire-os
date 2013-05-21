
#ifndef _DEVICEDB_H
#define _DEVICEDB_H

#include "ip.h"
#include "system.h"

typedef struct{
    uint16_t short_addr;
    uint64_t device_id;
    ip_addr_t ip;
} devdb_device_info_t;


void devdb_v_init( void );
void devdb_v_add_device( const devdb_device_info_t *info );
int8_t devdb_i8_get_device_by_short( uint16_t short_addr, devdb_device_info_t *info );
int8_t devdb_i8_get_device_by_index( uint16_t index, devdb_device_info_t *info );
uint16_t devdb_u16_get_device_count( void );



#endif

