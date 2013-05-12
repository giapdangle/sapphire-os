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


#ifndef _ADC_H
#define _ADC_H

#include "cpu.h"

// channels
#define ADC_CHANNEL_0           0
#define ADC_CHANNEL_1           1
#define ADC_CHANNEL_2           2
#define ADC_CHANNEL_3           3
#define ADC_CHANNEL_4           4
#define ADC_CHANNEL_5           5
#define ADC_CHANNEL_6           6
#define ADC_CHANNEL_7           7
#define ADC_CHANNEL_1V2_BG      8 // 1.2 volt bandgap
#define ADC_CHANNEL_0V0_AVSS    9 // 0.0 volt AVSS
#define ADC_CHANNEL_TEMP        10 // internal temperature sensor
#define ADC_CHANNEL_VSUPPLY     ( ADC_CHANNEL_0 ) // voltage monitor (connected to channel 0)


// reference selections
#define ADC_REF_AREF    0
#define ADC_REF_AVDD    1   // 1.8 volt internal supply
#define ADC_REF_1V5     2   // 1.5 volt reference
#define ADC_REF_1V6     3   // 1.6 volt reference

void adc_v_init( void );

float adc_f_read( uint8_t channel );
uint16_t adc_u16_read_mv( uint8_t channel );
uint16_t adc_u16_read_raw( uint8_t channel );

float adc_f_read_temperature( void );
float adc_f_read_supply_voltage( void );

uint16_t adc_u16_convert_to_millivolts( uint16_t raw_value );

#endif


