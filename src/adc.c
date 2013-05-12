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
#include "threading.h"
#include "timers.h"
#include "keyvalue.h"

#include "adc.h"


static int8_t kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len );

KV_SECTION_META kv_meta_t adc_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_VOLTAGE,     SAPPHIRE_TYPE_FLOAT,       KV_FLAGS_READ_ONLY, 0, kv_handler,  "supply_voltage" },
    { KV_GROUP_SYS_INFO, KV_ID_TEMP,        SAPPHIRE_TYPE_FLOAT,       KV_FLAGS_READ_ONLY, 0, kv_handler,  "board_temperature" },
};

static int8_t kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len )
{
    if( op == KV_OP_GET ){
        
        if( id == KV_ID_VOLTAGE ){

            float volts = adc_f_read_supply_voltage();
            memcpy( data, &volts, sizeof(volts) );
        }
        else if( id == KV_ID_TEMP ){
            
            float temp = adc_f_read_temperature();
            memcpy( data, &temp, sizeof(temp) );
        }
    }
    
    return 0;
}

void adc_v_init( void ){

    // set reference to 1.6v internal
    // channel 0, right adjusted
    ADMUX = ( 1 << REFS1 ) | ( 1 << REFS0 );
    
    // set tracking time to 4 cycles and maximum start up time
    ADCSRC = ( 1 << ADTHT1 ) | ( 1 << ADTHT0 ) | ( 1 << ADSUT4 ) |
             ( 1 << ADSUT3 ) | ( 1 << ADSUT2 ) | ( 1 << ADSUT1 ) | ( 1 << ADSUT0 );
    
    /*
    Timing notes:
    
    Fcpu = 16,000,000
    ADC prescaler = /8
    Fadc = 2,000,000
    
    Tracking time set to 4 cycles (2 microseconds)
    
    Total conversion time is 15 ADC cycles, 120 CPU cycles
    
    
    Clock for temperature sensor measurement: Fadc < 500,000
    Prescaler / 32
    */
}

float adc_f_read( uint8_t channel ){

    return (float)adc_u16_read_mv( channel ) / 1000.0;
}

uint16_t adc_u16_read_mv( uint8_t channel ){

    return adc_u16_convert_to_millivolts( adc_u16_read_raw( channel ) );
}

uint16_t adc_u16_read_raw( uint8_t channel ){
    
    // get ADMUX value and mask off channel bits
    uint8_t temp = ADMUX & 0b11000000;
    
    if( channel < 8 ){
        
        temp |= channel;
        
        // enable ADC, disable interrupt, auto trigger disabled
        // set prescaler to / 8
        ADCSRA = ( 1 << ADEN ) | ( 0 << ADATE ) | ( 0 << ADIE ) | 
                 ( 0 << ADPS2 ) | ( 1 << ADPS1 ) | ( 1 << ADPS0 );
    }
    else if( channel == ADC_CHANNEL_1V2_BG ){
        
        temp |= 0b00011110;
        
        // enable ADC, disable interrupt, auto trigger disabled
        // set prescaler to / 8
        ADCSRA = ( 1 << ADEN ) | ( 0 << ADATE ) | ( 0 << ADIE ) | 
                 ( 0 << ADPS2 ) | ( 1 << ADPS1 ) | ( 1 << ADPS0 );
    }
    else if( channel == ADC_CHANNEL_0V0_AVSS ){
        
        temp |= 0b00011111;
        
        // enable ADC, disable interrupt, auto trigger disabled
        // set prescaler to / 8
        ADCSRA = ( 1 << ADEN ) | ( 0 << ADATE ) | ( 0 << ADIE ) | 
                 ( 0 << ADPS2 ) | ( 1 << ADPS1 ) | ( 1 << ADPS0 );
    }
    else if( channel == ADC_CHANNEL_TEMP ){
        
        temp |= 0b00001001;
        
        // enable ADC, disable interrupt, auto trigger disabled
        // set prescaler to / 32
        ADCSRA = ( 1 << ADEN ) | ( 0 << ADATE ) | ( 0 << ADIE ) | 
                 ( 1 << ADPS2 ) | ( 0 << ADPS1 ) | ( 1 << ADPS0 );
    }
    else{
    
        ASSERT( FALSE );
    }
    
    // check if we need to set ADMUX5
    if( channel == ADC_CHANNEL_TEMP ){
        
        ADCSRB |= ( 1 << MUX5 );
    }
    else{
        
        ADCSRB &= ~( 1 << MUX5 );
    }
    
    // apply channel selection
    ADMUX = temp;
    
    // start conversion
    ADCSRA |= ( 1 << ADSC );
    
    // wait until conversion is complete
    while( !( ADCSRA & ( 1 << ADIF ) ) );
    
    // clear ADIF bit
    ADCSRA |= ( 1 << ADIF );
    
    // return result
    return ADC;
}

float adc_f_read_temperature( void ){
    
    uint32_t t = adc_u16_read_raw( ADC_CHANNEL_TEMP );
    
    /*
    Conversion (from datasheet):
    
    degrees C = (1.13 * ADC) - 272.8
    
    */
    
    return ( 1.13 * (float)t ) - 272.8;
}

float adc_f_read_supply_voltage( void ){
    
    uint32_t v = adc_u16_read_raw( ADC_CHANNEL_VSUPPLY );
    
    /*
    Conversion:
    
    pin voltage = ( ADC * Vref ) / 1024
    
    supply voltage = pin voltage * 11
    
    */
    
    return ( ( (float)v * 1.6 ) / 1024 ) * 11;
}


/*
// interrupt vector, for future use
ISR( ADC_vect ){
}
*/


uint16_t adc_u16_convert_to_millivolts( uint16_t raw_value ){
	
    // this calculation is for the 1.6v reference

	uint32_t millivolts;
	
	millivolts = (uint32_t)raw_value * 1600; 
    millivolts /= 1023;

	return (uint16_t)millivolts;
}

