/*
 
 SNTP Client

 Implements an RFC 4330 compliant SNTP client.
 
 Only unicast mode is supported.
 

 Notes:
 As written, the compiled implementation of this code is huge, around 10,000 bytes.
 This is because of all of the math with 64 bit variables, and with far too many
 to fit in the register set, so a lot of time is spent moving variables to/from 
 memory.


*/

#include "cpu.h"


#include "datetime.h"
#include "dns.h"
#include "config.h"
#include "fs.h"
#include "timers.h"
#include "threading.h"
#include "memory.h"
#include "sockets.h"
#include "system.h"

#include "appcfg.h"
#include "sntp.h"


//#define NO_LOGGING
#include "logging.h"


static ntp_ts_t network_time;
static uint32_t base_system_time; // system timer in ms last time network time was set

// offset and delay in ms from last sync
static int16_t last_offset;
static uint16_t last_delay;

static sntp_status_t8 status;

/*

Notes:

Network Time: This is the current time synchronized to the NTP server.
System Time: This is the current value of the 32 bit millisecond system timer.


Setting the clock:

We set network time from the calculation result of the SNTP request.
Base system time is the millisecond system timer value corresponding
to when we received the reply packet.  This allows us to correlate network
time with system time.

We drive network time at a 1 second rate from the system clock.
An API call to get network time with greater than 1 second of precision
will read the current system time, subtract the base system time, and
add those milliseconds to the network time.

Adjusting the system clock frequency:

Once we have a base system time set (after the initial network time sync),
we can calculate our local clock drift next time we sync to the server.
We calculate what the server says our local time should be and compare
that to the 1 millisecond precision local network time and the difference
tells us how many milliseconds we're off.  We can then calculate how much
to adjust the system clock frequency based on how much offset we need to
correct within the next polling interval.

Example:
Polling at 60 seconds
Initial offset is 0
Drifting at +0.1%

Assume our initial time is 0.

At our next sync, we have 60 seconds.  However, since we drifted +0.1%,
we're running too fast and we have 60.06 seconds.  We get 60 back from
the server, so we know we drift +0.06 seconds per polling interval.

Note we don't need to try to correct the offset itself, since we can
calculate our offset from the server.  We just want to make sure the offset
changes as little as possible between updates.

We increase our clock rate by 0.1%.
Next sync, we check our last offset and the new offset.  Say our new offset
is 0.07 seconds, we have difference of 0.01.  Still a little fast, so we
correct by (0.01 / 60) = 0.016%.

Note our accuracy is affected by our thread scheduling and network queuing delays.

It might be interesting to track statistics on the delays and offsets we get from the server.

*/


void process_packet( ntp_packet_t *packet );
PT_THREAD( sntp_client_thread( pt_t *pt, void *state ) );


ntp_ts_t sntp_t_now( void ){
        
    ntp_ts_t now = network_time;

    if( status == SNTP_STATUS_SYNCHRONIZED ){
    
        // get time elapsed since base time was set
        uint32_t elapsed_ms = tmr_u32_elapsed_time( base_system_time );
        
        ntp_ts_t elapsed = sntp_ts_from_ms( elapsed_ms );
        
        uint64_t a = ( (uint64_t)now.seconds << 32 ) + ( now.fraction );
        uint64_t b = ( (uint64_t)elapsed.seconds << 32 ) + ( elapsed.fraction );
        
        a += b;
        
        now.seconds = a >> 32;
        now.fraction = a & 0xffffffff;
    }

    return now;
}

ntp_ts_t sntp_t_last_sync( void ){

    return network_time;
}

void sntp_v_init( void ){
    
    // initialize network time
    network_time.seconds = 0xD0000000;
    network_time.fraction = 0;
    base_system_time = tmr_u32_get_system_time_ms();
        
    // check if SNTP is enabled
    if( cfg_b_get_boolean( CFG_PARAM_ENABLE_SNTP ) ){

        status = SNTP_STATUS_NO_SYNC;
            
        thread_t_create( sntp_client_thread,
                         PSTR("sntp_client"),
                         0,
                         0 );
    }
    else{

        status = SNTP_STATUS_DISABLED;
    }
}

sntp_status_t8 sntp_u8_get_status( void ){
    
    return status;
}

int16_t sntp_i16_get_offset( void ){
    
    return last_offset;
}

uint16_t sntp_u16_get_delay( void ){
    
    return last_delay;
}

/*
 Timestamp Conversions


From the RFC:

 Timestamp Name          ID   When Generated
      ------------------------------------------------------------
      Originate Timestamp     T1   time request sent by client
      Receive Timestamp       T2   time request received by server
      Transmit Timestamp      T3   time reply sent by server
      Destination Timestamp   T4   time reply received by client

   The roundtrip delay d and system clock offset t are defined as:

      d = (T4 - T1) - (T3 - T2)     t = ((T2 - T1) + (T3 - T4)) / 2.
      
*/

ntp_ts_t sntp_ts_from_ms( uint32_t ms ){
    
    ntp_ts_t n;
    
    n.seconds = ms / 1000;
    n.fraction = ( ( ( ms % 1000 ) * 1000 ) / 1024 ) << 22;
    
    return n;
}

uint16_t sntp_u16_get_fraction_as_ms( ntp_ts_t t ){
    
    uint64_t frac = t.fraction * 1000;
    
    frac /= ( 2^32 );
    
    return (uint16_t)frac;
}


void process_packet( ntp_packet_t *packet ){
    
    // get new base system time, we'll hang on to this until the end of the time calculations
    uint32_t new_base_system_time = tmr_u32_get_system_time_ms();
    
    // get destination timestamp (our local network time when we receive the packet)
    ntp_ts_t dest_ts = sntp_t_now();
    
    uint64_t dest_timestamp = ( (uint64_t)dest_ts.seconds << 32 ) + dest_ts.fraction;
    
    // get timestamps from packet, noting the conversion from big endian to little endian
    uint64_t originate_timestamp =  ( (uint64_t)( HTONL( packet->originate_timestamp.seconds ) ) << 32 ) +
                                                ( HTONL( packet->originate_timestamp.fraction ) );
        
    uint64_t receive_timestamp =    ( (uint64_t)( HTONL( packet->receive_timestamp.seconds ) ) << 32 ) +
                                                ( HTONL( packet->receive_timestamp.fraction ) );
    
    uint64_t transmit_timestamp =   ( (uint64_t)( HTONL( packet->transmit_timestamp.seconds ) ) << 32 ) +
                                                ( HTONL( packet->transmit_timestamp.fraction ) );
    
    int64_t delay = ( dest_timestamp - originate_timestamp ) - ( transmit_timestamp - receive_timestamp );
    
    int64_t offset = ( (int64_t)( receive_timestamp - originate_timestamp ) + (int64_t)( transmit_timestamp - dest_timestamp ) ) / 2;
    
    // current network time is originate time + delay + offset
    uint64_t current_time = originate_timestamp + delay + offset;
    
    // set offset and delay variables
    if( ( ( offset >> 32 ) < 32 ) && ( ( offset >> 32 ) > -32 ) ){
        
        last_offset = ( ( offset >> 32 ) * 1000 ) + ( ( offset & 0xffffffff ) % 1000 );
    }
    else{
        
        last_offset = 0;
    }
    
    if( ( delay >> 32 ) < 64 ){
    
        last_delay = ( ( delay >> 32 ) * 1000 ) + ( ( delay & 0xffffffff ) % 1000 );
    }
    else{
        
        last_delay = 0;
    }
    
    // set network time
    network_time.seconds = current_time >> 32;
    network_time.fraction = current_time & 0xffffffff;

    // set base system time
    base_system_time = new_base_system_time;
    
    // set sync status
    status = SNTP_STATUS_SYNCHRONIZED;
}




PT_THREAD( sntp_client_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;
    static socket_t sock;
    static uint8_t tries;
    
    
	while(1){
        
        // wait if IP is not configured
        THREAD_WAIT_WHILE( pt, !cfg_b_ip_configured() );
        
        // check DNS resolver for ip address and set up remote port
        sock_addr_t ntp_server_addr;
        ntp_server_addr.port = SNTP_SERVER_PORT;
        
        char name[CFG_STR_LEN];
        cfg_i8_get( CFG_PARAM_SNTP_SERVER, name );
        
        ntp_server_addr.ipaddr = dns_a_query( name );
        
        if( ip_b_is_zeroes( ntp_server_addr.ipaddr ) ){
            
            timer = 1000;
            TMR_WAIT( pt, timer );

            // that's too bad, we'll have to skip this cycle and try again later
            THREAD_RESTART( pt );
        }


        // get socket
        sock = sock_s_create( SOCK_DGRAM );
        
        // check socket creation
        if( sock < 0 ){
            
            // that's too bad, we'll have to skip this cycle and try again later
            THREAD_RESTART( pt );
        }
        
        tries = SNTP_TRIES;

        while( tries > 0 ){

            // build sntp packet
            ntp_packet_t pkt;
            
            // initialize to all 0s
            memset( &pkt, 0, sizeof(pkt) );
            
            // set version to 4 and mode to client
            pkt.li_vn_mode = SNTP_VERSION_4 | SNTP_MODE_CLIENT;
            
            // get our current network time with the maximum available precision
            ntp_ts_t transmit_ts = sntp_t_now();
            
            // set transmit timestamp (converting from little endian to big endian)
            pkt.transmit_timestamp.seconds = HTONL(transmit_ts.seconds);
            pkt.transmit_timestamp.fraction = HTONL(transmit_ts.fraction);
            
            // send packet
            // if packet transmission fails, we'll try again on the next polling cycle
            if( sock_i16_sendto( sock, &pkt, sizeof(pkt), &ntp_server_addr ) < 0 ){
                
                goto clean_up;
            }

            log_v_debug_P( PSTR("SNTP sync sent") );
            
            // set timeout
            timer = tmr_u32_get_system_time() + SNTP_TIMEOUT;
            
            // wait for packet
            THREAD_WAIT_WHILE( pt, ( sock_i8_recvfrom( sock ) < 0 ) &&
                                   ( tmr_i8_compare_time( timer ) > 0 ) );
            
            // check for timeout (no data received)
            if( sock_i16_get_bytes_read( sock ) >= 0 ){
                
                break;
            }
            else{

                log_v_debug_P( PSTR("SNTP sync timed out") );
                
                tries--;

                if( tries == 0 ){
                    
                    goto clean_up;
                }
            }
        }


        log_v_debug_P( PSTR("SNTP sync received") );

        // get data and process it
        // NOTE the original ntp packet local variable we used will
        // be corrupt at this point in the thread, DO NOT USE IT!
        ntp_packet_t *recv_pkt = sock_vp_get_data( sock );
        
        // process received packet
        process_packet( recv_pkt );
        

        // parse current time to ISO so we can read it in the log file
        datetime_t datetime;
        datetime_v_seconds_to_datetime( network_time.seconds, &datetime );

        char time_str[ISO8601_STRING_MIN_LEN];
        datetime_v_to_iso8601( time_str, sizeof(time_str), &datetime );
        
        log_v_info_P( PSTR("NTP Time is now: %s Offset: %d Delay: %d"), time_str, last_offset, last_delay );
        

clean_up:        
        sock_v_release( sock );
    
        uint16_t sync_interval;
        
        // get sync interval from config database
        cfg_i8_get( CFG_PARAM_SNTP_SYNC_INTERVAL, &sync_interval );
        
        // bounds check sync interval
        if( sync_interval < SNTP_MINIMUM_POLL_INTERVAL ){
            
            sync_interval = SNTP_MINIMUM_POLL_INTERVAL;

            // store minimum sync to config database
            cfg_v_set( CFG_PARAM_SNTP_SYNC_INTERVAL, &sync_interval );
        }

        timer = (uint32_t)sync_interval * 1000; // convert interval to milliseconds
        
        // wait during polling interval
        TMR_WAIT( pt, timer );
    }

PT_END( pt );
}











