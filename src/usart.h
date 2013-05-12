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

#ifndef _USART_H
#define _USART_H

#define USART_DEFAULT_BAUD  115200

void us0_v_init( void );

void us0_v_set_baud( uint32_t baud );

bool us0_b_received_char( void );

void us0_v_send_char( uint8_t data );
void us0_v_send_data( const uint8_t *data, uint8_t len );

int16_t us0_i16_get_char( void );

#endif

