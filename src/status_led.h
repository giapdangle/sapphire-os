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

#ifndef _STATUS_LED_H
#define _STATUS_LED_H


void status_led_v_init( void );
void status_led_v_set_blink_speed( uint16_t speed ); 
#define LED_BLINK_NORMAL    500

#endif

