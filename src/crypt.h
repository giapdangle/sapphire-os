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


#ifndef _CRYPT_H
#define _CRYPT_H

#define CRYPT_KEY_SIZE          16
#define CRYPT_AUTH_TAG_SIZE     12


void crypt_v_aes_xcbc_mac_96( uint8_t k[CRYPT_KEY_SIZE], uint8_t *m, uint16_t n, uint8_t auth[CRYPT_AUTH_TAG_SIZE] );
void crypt_v_aes_cbc_128_encrypt( uint8_t k[CRYPT_KEY_SIZE], uint8_t iv[CRYPT_KEY_SIZE], uint8_t *m, uint16_t n, uint8_t *o, uint16_t o_buf_size );
void crypt_v_aes_cbc_128_decrypt( uint8_t k[CRYPT_KEY_SIZE], uint8_t iv[CRYPT_KEY_SIZE], uint8_t *m, uint16_t n, uint8_t *o, uint16_t o_buf_size );










#endif

