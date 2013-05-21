
#include "cpu.h"

#include "threading.h"
#include "config.h"
#include "timers.h"
#include "wcom_time.h"

#include "appcfg.h"

#include "sntp.h"
#include "time_source.h"

static uint8_t sequence;

PT_THREAD( time_source_thread( pt_t *pt, void *state ) );


void timesource_v_init( void ){
        
    if( cfg_b_get_boolean( CFG_PARAM_ENABLE_TIME_SOURCE ) ){

        thread_t_create( time_source_thread,
                         PSTR("time_source"),
                         0,
                         0 );
    }
}


PT_THREAD( time_source_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  

    static uint32_t timer;
    
    while(1){
        
        // advance sequence
        sequence++;
        
        uint32_t now = tmr_u32_get_system_time_us();

        // set sync
        wcom_time_v_sync( cfg_u16_get_short_addr(),
                          0,
                          WCOM_TIME_SOURCE_GATEWAY,
                          sequence,
                          now,
                          now,
                          sntp_t_now() );
        
        // wait for next interval
        timer = TIME_SOURCE_DELAY_MS;
        TMR_WAIT( pt, timer );
    }

PT_END( pt );
}


