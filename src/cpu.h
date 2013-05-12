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
 
#ifndef _CPU_H_
#define _CPU_H_


#ifdef __SIM__

    #include <stdio.h>
    #include <stdint.h>
    
    #define PGM_P void*
    #define PROGMEM
    #define PSTR(s) ({static char c[ ] = (s); c;})
    
    #define strncpy_P strncpy
    #define strncmp_P strncmp
    #define strcmp_P strcmp
    #define strlen_P strlen
    #define memcpy_P memcpy
    #define strnlen(s, n) strlen(s) // NOT SAFE!!!
    #define strstr_P strstr
    
    #define sprintf_P sprintf
    
    //#define pgm_read_word(x) *(uint16_t *)x
    //#define pgm_read_byte_far(x) *(uint8_t *)x
    uint16_t pgm_read_word(void *a);
    uint8_t pgm_read_byte_far(void *a);
    
    #define wdt_reset()
    #define _delay_us(x)
    
    #define cli()
    
#else
    #include <avr/io.h>
    #include <avr/pgmspace.h>
    #include <avr/interrupt.h>
    #include <avr/eeprom.h>
    #include <avr/wdt.h>
    #include <avr/sleep.h>
    #include <util/delay.h>
    #include <stdio.h>
    #include <string.h>
#endif


#endif

