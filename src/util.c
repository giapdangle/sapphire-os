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


#include "util.h"

// yes, we're writing our own fabs because for some reason avr-libc doesn't actually have it
float f_abs( float x ){
    
    if( x < 0.0 ){
        
        x *= -1.0;
    }

    return x;
}

