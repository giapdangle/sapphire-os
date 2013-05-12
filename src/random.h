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



#ifndef _RANDOM_H
#define _RANDOM_H

void rnd_v_init( void );
void rnd_v_seed( uint32_t seed );
uint16_t rnd_u16_get_int( void );
uint16_t rnd_u16_get_int_hw( void );
void rnd_v_fill( void *data, uint16_t len );

#endif






