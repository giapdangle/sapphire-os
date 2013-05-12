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

#ifndef _SW_TEST_H_
#define _SW_TEST_H_

#include <stdio.h>


void sw_test_v_init( void );

void sw_test_v_start( PGM_P module_name );
void sw_test_finish( void );

void sw_test_v_assert_true( bool expr, PGM_P expr_str, PGM_P file, uint16_t line );
void sw_test_v_assert_int_equals( int32_t actual, int32_t expected, PGM_P file, uint16_t line );


#define TEST_ASSERT_TRUE(expr) sw_test_v_assert_true( (expr), PSTR( #expr ), PSTR( __FILE__ ), __LINE__)
#define TEST_ASSERT_EQUALS(actual, expected) sw_test_v_assert_int_equals( (int32_t)(actual), (int32_t)(expected), PSTR( __FILE__ ), __LINE__)


#endif

