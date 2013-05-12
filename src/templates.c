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


#if 0 // don't compile this module!

// "one-shot" thread
PT_THREAD( one_shot_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    // do stuff...
    
PT_END( pt );
}

// daemon thread
PT_THREAD( daemon_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while(1){
        
        // stuff goes here...

        THREAD_YIELD( pt );
    }

PT_END( pt );
}

// infinite server thread
PT_THREAD( server_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    // create socket
	static socket_t sock;
    sock = sock_s_create( SOCK_DGRAM );
    
    // assert if socket was not created
    ASSERT( sock >= 0 );

	// bind to port
	sock_v_bind( sock, PORT );

    while(1){
            
		// wait for a datagram
        THREAD_WAIT_WHILE( pt, sock_i8_recvfrom( sock ) < 0 );
        
		// get data
		uint8_t *type = sock_vp_get_data( sock );
        
        // process data

        
    }

PT_END( pt );
}


#endif

