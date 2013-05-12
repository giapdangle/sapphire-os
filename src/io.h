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

#ifndef _IO_H
#define _IO_H

#include "system.h"

#define HIGH ( TRUE )
#define LOW  ( FALSE )

#define IO_BUTTON_CHECK_INTERVAL	1000
#define IO_BUTTON_TRIGGER_THRESHOLD	10
#define IO_BUTTON_RESTART_TIME		5 

#define IO_TYPE_INPUT       0x01
#define IO_TYPE_OUTPUT      0x02
#define IO_TYPE_ANALOG_IN   0x04
#define IO_TYPE_ANALOG_OUT  0x08 // PWM

typedef uint8_t io_mode_t8;
#define IO_MODE_INPUT           0
#define IO_MODE_INPUT_PULLUP    1
#define IO_MODE_OUTPUT          2

typedef uint8_t io_int_mode_t8;
#define IO_INT_LOW              0
#define IO_INT_CHANGE           1
#define IO_INT_FALLING          2
#define IO_INT_RISING           3

#define IO_N_INT_HANDLERS       4
typedef void (*io_int_handler_t)( void );

// IO definitions
// pin name to logical (not physical) pin number mapping

#define IO_PIN_COUNT        29

#define IO_PIN_HW_ID        28

#define IO_PIN_LED2         27
#define IO_PIN_LED1         26
#define IO_PIN_LED0         25

#define IO_PIN_S3           24
#define IO_PIN_S2           23
#define IO_PIN_S1           22
#define IO_PIN_S0           21

#define IO_PIN_GPIO7        20
#define IO_PIN_GPIO6        19
#define IO_PIN_GPIO5        18
#define IO_PIN_GPIO4        17
#define IO_PIN_GPIO3        16
#define IO_PIN_GPIO2        15
#define IO_PIN_GPIO1        14
#define IO_PIN_GPIO0        13

#define IO_PIN_ADC7         12
#define IO_PIN_ADC6         11
#define IO_PIN_ADC5         10
#define IO_PIN_ADC4         9
#define IO_PIN_ADC3         8
#define IO_PIN_ADC2         7
#define IO_PIN_ADC1         6
#define IO_PIN_ADC0         5

#define IO_PIN_PWM2         4
#define IO_PIN_PWM1         3
#define IO_PIN_PWM0         2

#define IO_PIN_TX           1
#define IO_PIN_RX           0

// LED color definitions
#define IO_PIN_LED_GREEN    IO_PIN_LED0
#define IO_PIN_LED_YELLOW   IO_PIN_LED1
#define IO_PIN_LED_RED      IO_PIN_LED2


void io_v_init( void );
uint8_t io_u8_get_board_rev( void );

void io_v_set_mode( uint8_t pin, io_mode_t8 mode );
void io_v_digital_write( uint8_t pin, bool state );

bool io_b_digital_read( uint8_t pin );

bool io_b_button_down( void );
void io_v_disable_jtag( void );

void io_v_enable_interrupt( 
    uint8_t int_number, 
    io_int_handler_t handler,
    io_int_mode_t8 mode );

void io_v_disable_interrupt( uint8_t int_number );

#endif

