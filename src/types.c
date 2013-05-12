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
#include "types.h"



uint16_t type_u16_size( sapphire_type_t8 type ){
    
    uint16_t size = SAPPHIRE_TYPE_INVALID;

    switch( type){
        case SAPPHIRE_TYPE_NONE:
            size = 0;
            break;

        case SAPPHIRE_TYPE_BOOL:
        case SAPPHIRE_TYPE_UINT8:
        case SAPPHIRE_TYPE_INT8:
            size = 1;
            break;

        case SAPPHIRE_TYPE_UINT16:
        case SAPPHIRE_TYPE_INT16:
            size = 2;
            break;

        case SAPPHIRE_TYPE_UINT32:
        case SAPPHIRE_TYPE_INT32:
        case SAPPHIRE_TYPE_FLOAT:
        case SAPPHIRE_TYPE_IPv4:
            size = 4;
            break;

        case SAPPHIRE_TYPE_UINT64:
        case SAPPHIRE_TYPE_INT64:
            size = 8;
            break;

        case SAPPHIRE_TYPE_STRING128:
            size = 128;
            break;

        case SAPPHIRE_TYPE_STRING512:
            size = 512;
            break;

        case SAPPHIRE_TYPE_MAC48:
            size = 6;
            break;

        case SAPPHIRE_TYPE_MAC64:
            size = 8;
            break;

        case SAPPHIRE_TYPE_KEY128:
            size = 16;
            break;
        
    }

    return size;
}




