/********************************************************
			Sapphire Ethernet Gateway
	
	(C)2007 by Jeremy Billheimer
********************************************************/

#include "cpu.h"

#include "target.h"
#include "system.h"
	
#include "init.h"
#include "threading.h"
#include "timers.h"
#include "config.h"
#include "adc.h"
#include "sntp.h"
#include "dhcp.h"
#include "enc28j60.h"
#include "bridging.h"
#include "gateway_services.h"
#include "gateway_server.h"
#include "gateway.h"
#include "arp.h"
#include "io.h"
#include "devicedb.h"
#include "appcfg.h"

#include "dns.h"

    
void main( void ) __attribute__ ((noreturn));

__attribute__ ((section (".fwinfo"))) fw_info_t fw_info;

void main( void ){		
        
    if( sapphire_i8_init() == 0 ){
        
    }

    // init ethernet io pins
	eth_v_io_init();	

	// init ethernet    
	if( eth_u8_init() == 0 ){
        
        io_v_set_mode( IO_PIN_GPIO0, IO_MODE_OUTPUT );
        io_v_digital_write( IO_PIN_GPIO0, HIGH );
    }
    
    // init ARP 
    arp_v_init();   
        
    // init gateway
    gateway_v_init();

    // init DNS
    dns_v_init();


    sapphire_run();

	// should never get here:
	for(;;);
}   

// (C)2007-2013 by Jeremy Billheimer
    
