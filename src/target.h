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

#ifndef _TARGET_H
#define _TARGET_H

#define HW_NAME "Sapphire4"


// bootloader target settings
// page size in bytes
#define PAGE_SIZE 256
// number of pages in app section
#define N_APP_PAGES 480
// total pages
#define TOTAL_PAGES 512
// loader jump address
#define LDR_JMP asm("jmp 0xf800") // word address, not byte address

// asserts
// comment this out to turn off compiler asserts
#define INCLUDE_COMPILER_ASSERTS

// comment this out to turn off run-time asserts
#define INCLUDE_ASSERTS

// loader.c LED pins
#define LDR_LED_GREEN_DDR       DDRD
#define LDR_LED_GREEN_PORT      PORTD
#define LDR_LED_GREEN_PIN       5
#define LDR_LED_YELLOW_DDR      DDRD
#define LDR_LED_YELLOW_PORT     PORTD
#define LDR_LED_YELLOW_PIN      6
#define LDR_LED_RED_DDR         DDRD
#define LDR_LED_RED_PORT        PORTD
#define LDR_LED_RED_PIN         7

// spi.c
// SPI port settings:
#define SPI_DDR DDRB
#define SPI_PORT PORTB
#define SPI_PIN PINB
#define SPI_SS PINB0
#define SPI_MOSI PINB2
#define SPI_MISO PINB3
#define SPI_SCK PINB1


#endif
