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

#ifndef __TYPES_H
#define __TYPES_H

typedef int8_t sapphire_type_t8;

#define SAPPHIRE_TYPE_MAX_LEN           512

#define SAPPHIRE_TYPE_INVALID           65535

#define SAPPHIRE_TYPE_NONE              0
#define SAPPHIRE_TYPE_BOOL              1
#define SAPPHIRE_TYPE_UINT8             2
#define SAPPHIRE_TYPE_INT8              3
#define SAPPHIRE_TYPE_UINT16            4
#define SAPPHIRE_TYPE_INT16             5
#define SAPPHIRE_TYPE_UINT32            6
#define SAPPHIRE_TYPE_INT32             7
#define SAPPHIRE_TYPE_UINT64            8
#define SAPPHIRE_TYPE_INT64             9
#define SAPPHIRE_TYPE_FLOAT             10

#define SAPPHIRE_TYPE_STRING128         40
#define SAPPHIRE_TYPE_MAC48             41
#define SAPPHIRE_TYPE_MAC64             42
#define SAPPHIRE_TYPE_KEY128            43
#define SAPPHIRE_TYPE_IPv4              44
#define SAPPHIRE_TYPE_STRING512         45


uint16_t type_u16_size( sapphire_type_t8 type );

#endif

