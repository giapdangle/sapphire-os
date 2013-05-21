
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#include "target.h"

#include "system.h"

#include "config.h"
#include "timers.h"
#include "threading.h"
#include "statistics.h"
#include "netmsg.h"

#include "spi.h"
#include "enc28j60.h"
#include "io.h"

#include "appcfg.h"

#include <string.h>


/*		

ENC28J60 SPI Ethernet Controller Driver

*/

#define set_cs();       if( rev_4_4 ){ ETH_4_4_CS_PORT |= _BV( ETH_4_4_CS_PIN ); } \
                        else{ ETH_CS_PORT |= _BV( ETH_CS_PIN ); }

#define clear_cs();     if( rev_4_4 ){ ETH_4_4_CS_PORT &= ~_BV( ETH_4_4_CS_PIN ); } \
                        else{ ETH_CS_PORT &= ~_BV( ETH_CS_PIN ); }

// function pointer to handle a received frame.
// this must be set by the user BEFORE the transceiver is activated
void ( *eth_v_receive_frame )( uint16_t type_len, netmsg_t netmsg );

static bool enabled = FALSE;

static uint16_t next_packet_ptr;
static uint16_t rx_status;


static bool tx_busy;
static uint32_t tx_timestamp;

static uint8_t eth_irq;

static bool rev_4_4;


static void irq_handler( void );

PT_THREAD( eth_irq_thread( pt_t *pt, void *state ) );
PT_THREAD( eth_tx_monitor_thread( pt_t *pt, void *state ) );
PT_THREAD( eth_irq_check_thread( pt_t *pt, void *state ) );

// call this init function before using any devices on the SPI bus
void eth_v_io_init( void ){
	
    rev_4_4 = FALSE;

    // check board rev:
    // set GPIO1 to pull up
    io_v_set_mode( IO_PIN_GPIO1, IO_MODE_INPUT_PULLUP );
    
    if( io_b_digital_read( IO_PIN_GPIO1 ) == FALSE ){
        
        rev_4_4 = TRUE;
    }
    
    if( rev_4_4 ){

        // set up io pins:
        ETH_4_4_CS_DDR |= _BV( ETH_4_4_CS_PIN );
        ETH_4_4_RST_DDR |= _BV( ETH_4_4_RST_PIN );
        ETH_4_4_IRQ_DDR &= ~_BV( ETH_4_4_IRQ_PIN );

        ETH_4_4_RST_PORT &= ~_BV( ETH_4_4_RST_PIN );

        ETH_4_4_RST_PORT |= _BV( ETH_4_4_RST_PIN );
    }
    else{

        // set up io pins:
        ETH_CS_DDR |= _BV( ETH_CS_PIN );
        ETH_RST_DDR |= _BV( ETH_RST_PIN );
        ETH_IRQ_DDR &= ~_BV( ETH_IRQ_PIN );

        ETH_RST_PORT &= ~_BV( ETH_RST_PIN );

        ETH_RST_PORT |= _BV( ETH_RST_PIN );
    }

    set_cs();
}

uint8_t low_level_init( void ){
	
	// SPI must be initialized before calling this function!
	
    if( rev_4_4 ){

        ETH_4_4_RST_PORT &= ~_BV( ETH_4_4_RST_PIN );
        
        _delay_ms( 5 );
        
        ETH_4_4_RST_PORT |= _BV( ETH_4_4_RST_PIN );
    }
    else{

        ETH_RST_PORT &= ~_BV( ETH_RST_PIN );
        
        _delay_ms( 5 );
        
        ETH_RST_PORT |= _BV( ETH_RST_PIN );
    }

	// reset the chip
	eth_v_reset();
	
	// poll for clock ready.  return an error if it takes too long
	uint16_t i = 0;
	
	while( ( eth_u8_read_reg( ESTAT ) & ESTAT_CLKRDY ) == 0 ){
		
		i++;
		
		_delay_ms(1);
		
		if( i >= 10 ){
			
			return 1;
		}
	}
	
	// initialize the receive buffer
	
	// set RX start position and pointers
	eth_v_write_reg( ERXSTL, ETH_RX_START & 0xff );
	eth_v_write_reg( ERXSTH, ETH_RX_START >> 8 );
	eth_v_write_reg( ERXRDPTL, ETH_RX_START & 0xff );
	eth_v_write_reg( ERXRDPTH, ETH_RX_START >> 8 );
	
	next_packet_ptr = 0;
	
	// set RX end
	eth_v_write_reg( ERXNDL, ETH_RX_END & 0xff );
	eth_v_write_reg( ERXNDH, ETH_RX_END >> 8 );
	
	// set TX start
	eth_v_write_reg( ETXSTL, ETH_TX_START & 0xff );
	eth_v_write_reg( ETXSTH, ETH_TX_START >> 8 );
	
	// set TX end
	eth_v_write_reg( ETXNDL, ETH_TX_END & 0xff );
	eth_v_write_reg( ETXNDH, ETH_TX_END >> 8 );
	
	
	// set up MAC
	// enable full duplex flow control and the receiver
	eth_v_set_bits( MACON1, MACON1_TXPAUS | MACON1_RXPAUS | MACON1_MARXEN );
	
	// pad to 60 bytes and append CRC, transmit CRC, enable frame length checking, enable full duplex
	eth_v_set_bits( MACON3, MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN | MACON3_FULDPX );
	
	// set deferred transmission
	eth_v_set_bits( MACON4, MACON4_DEFER );
	
	// set max frame length
	eth_v_write_reg( MAMXFLL, ETH_MAX_FRAME_LEN & 0xff );
	eth_v_write_reg( MAMXFLH, ETH_MAX_FRAME_LEN >> 8 );
	
	// set back to back interpacket gap
	eth_v_write_reg( MABBIPG, 0x15 ); // 0x15 recommended in datasheet
	
	// set non back to back interpacket gap
	eth_v_write_reg( MAIPGL, 0x12 ); // 0x12 recommended in datasheet
	
	// set MAC address
	eth_mac_addr_t addr;
	
	cfg_i8_get( CFG_PARAM_ETH_MAC_ADDRESS, &addr );
	
	eth_v_write_reg( MAADR6, addr.mac[5] );
	eth_v_write_reg( MAADR5, addr.mac[4] );
	eth_v_write_reg( MAADR4, addr.mac[3] );
	eth_v_write_reg( MAADR3, addr.mac[2] );
	eth_v_write_reg( MAADR2, addr.mac[1] );
	eth_v_write_reg( MAADR1, addr.mac[0] );
	
	// read PHCON1
	uint16_t phcon1 = eth_u16_read_phy( PHCON1 );
	
	// enable full duplex
	phcon1 |= PHCON1_PDPXMD;
	
	// write back to PHCON1
	eth_v_write_phy( PHCON1, phcon1 );
	
	// disable loopback
	//eth_v_write_phy( PHCON2, PHCON2_HDLDIS );
	
	//eth_v_write_reg( ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_BCEN );
	
	// enable interrupts
	// interrupts, receive and transmit enabled
	eth_v_write_reg( EIE, EIE_INTIE | EIE_PKTIE | EIE_TXIE | EIE_TXERIE | EIE_RXERIE );
	
	// enable receive
	eth_v_set_bits( ECON1, ECON1_RXEN );
	
	return 0;
}


uint8_t eth_u8_init( void ){
	
	if( low_level_init() != 0 ){
		
		return 1;
	}

	enabled = TRUE;
	
	// create the IRQ handler thread
	thread_t_create( eth_irq_thread,
                     PSTR("ethernet_irq"),
                     0,
                     0 );
	
	// create the transmit timeout monitor
	thread_t_create( eth_tx_monitor_thread,
                     PSTR("ethernet_tx_monitor"),
                     0,
                     0 );
	
	thread_t_create( eth_irq_check_thread,
                     PSTR("ethernet_irq_monitor"),
                     0,
                     0 );
	
    return 0;
}

bool eth_b_enabled( void ){
	
	return enabled;
}

static void irq_handler( void ){

    thread_v_signal( SIGNAL_ETH_IRQ );
}

ISR( ETH_IRQ_VECTOR ){

    //stats_v_increment( STAT_ETH_IRQS_HANDLED );
    irq_handler();
}

PT_THREAD( eth_irq_check_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
	while(1){
		
		static uint32_t time_wait;
		
		time_wait = 50;
		
		TMR_WAIT( pt, time_wait );
		
		// set interrupt bits, but don't clear pending bits
		ATOMIC;
		eth_irq = eth_u8_read_reg( EIR );
		END_ATOMIC;
		
		// eth_irq if any irq bits are set, if so, post the irq thread
		if( eth_irq != 0 ){
	
            thread_v_signal( SIGNAL_ETH_IRQ );
			
           //stats_v_increment( STAT_ETH_RX_ERRORS );
		}
	}
	
PT_END( pt );
}


PT_THREAD( eth_irq_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
	// enable external interrupt
	// we enable the interrupt in the IRQ thread, this way the IRQ won't
	// be fired until after the IRQ thread has had a chance to initialize

    if( rev_4_4 ){
        
        // attach interrupt handlers
        io_v_enable_interrupt( 0, &irq_handler, IO_INT_FALLING );
    }
    else{
	
        ETH_IRQ_CTRL_REG |= ETH_IRQ_CTRL_BITS;
        EIMSK |= ETH_IRQ_MSK_BITS;
    }

	while(1){
		
		THREAD_WAIT_SIGNAL( pt, SIGNAL_ETH_IRQ );
		
		// disable the global interrupt
		eth_v_clear_bits( EIE, EIE_INTIE );
		
		eth_irq = eth_u8_read_reg( EIR );
		
	//	do{
			
			if( eth_irq & EIR_TXIF ){
				
                //stats_v_increment( STAT_ETH_SENT );
				
				tx_busy = FALSE;
				
				// clear the irq flag
				eth_v_clear_bits( EIR, EIR_TXIF );
			}
			
			// check for transmit errors
			if( eth_irq & EIR_TXERIF ){
				
                //stats_v_increment( STAT_ETH_TX_ERRORS );
				
				// clear the irq flag
				eth_v_clear_bits( EIR, EIR_TXERIF );
			}
			
			// check for receive errors
			if( eth_irq & EIR_RXERIF ){
				
				//stats_v_increment( STAT_ETH_RX_ERRORS );
				
				// clear the irq flag
				eth_v_clear_bits( EIR, EIR_RXERIF );
			}
			
			// check recieved packet bit
			if( eth_irq & EIR_PKTIF ){
				
				// clear the irq flag
				eth_v_clear_bits( EIR, EIR_PKTIF );
			}
			
			// check rx packet counter.
			// sometimes the interrupt flag won't be set by the controller,
			// so just check the packet counter directly and read all pending
			// packets
			uint8_t pktcnt = eth_u8_read_reg( EPKTCNT );
			
			// read all pending packets
			while( pktcnt > 0 ){
				
                // read next packet
				if( eth_i8_read_packet() < 0 ){
                    
                    // if packet couldn't be queued, break loop since anything else will just be thrown away
                    break;
                }
				
				pktcnt = eth_u8_read_reg( EPKTCNT );
			}
			
			// check if another interrupt is pending
			//eth_irq = eth_u8_read_reg( EIR );
			
			// yield to the scheduler to prevent the hardware from locking up the processor
			//PT_YIELD( pt );
				
		//} while( ( eth_irq & (EIR_TXIF|EIR_PKTIF) ) != 0 );
	
		// enable the global interrupt
		eth_v_set_bits( EIE, EIE_INTIE );
	}
	
PT_END( pt );
}

bool eth_b_tx_busy( void ){
	
	return tx_busy;
}

// set bank based on register address
void eth_v_set_bank( uint8_t addr ){
	
    // clear the bank sel bits
    clear_cs();

    spi_u8_send( ETH_SPI_OP_BFC | ( ECON1 & ADDR_MSK ) );

    spi_u8_send( ECON1_BSEL1 | ECON1_BSEL0 );

    set_cs();


    // set the bank sel bits
    clear_cs();

    spi_u8_send( ETH_SPI_OP_BFS | ( ECON1 & ADDR_MSK ) );

    spi_u8_send( ( addr & BANK_MSK ) >> 5 );

    set_cs();
}	

// write to a control reg.
// this will automatically set the correct bank
void eth_v_write_reg( uint8_t addr, uint8_t data ){
	
	// set bank
	eth_v_set_bank( addr );
	
	// write to the control reg
	clear_cs();
	
	spi_u8_send( ETH_SPI_OP_WCR | ( addr & ADDR_MSK ) );
	
	spi_u8_send( data );
	
	set_cs();
}

void eth_v_set_bits( uint8_t addr, uint8_t bits ){
	
	// set bank
	eth_v_set_bank( addr );
	
	// write to the control reg
	clear_cs();
	
	spi_u8_send( ETH_SPI_OP_BFS | ( addr & ADDR_MSK ) );
	
	spi_u8_send( bits );
	
	set_cs();
}

void eth_v_clear_bits( uint8_t addr, uint8_t bits ){
	
	// set bank
	eth_v_set_bank( addr );
	
	// write to the control reg
	clear_cs();
	
	spi_u8_send( ETH_SPI_OP_BFC | ( addr & ADDR_MSK ) );
	
	spi_u8_send( bits );
	
	set_cs();
}

uint8_t eth_u8_read_reg( uint8_t addr ){
	
	// set bank
	eth_v_set_bank( addr );
	
	clear_cs();
	
	spi_u8_send( ETH_SPI_OP_RCR | ( addr & ADDR_MSK ) );
	
	// check if this is a MAC or PHY register
	// if so, we need a dummy byte
	if( ( addr & MAC_PHY ) != 0 ){
		
		spi_u8_send( 0 );
	}
	
	uint8_t data = spi_u8_send( 0 );
	
	set_cs();
	
	return data;
}

void eth_v_reset( void ){
	
	clear_cs();
	
	spi_u8_send( ETH_SPI_OP_SRC );
	
	set_cs();
}

void eth_v_read_buffer( uint8_t *data, uint16_t length ){
	
	clear_cs();
	
	spi_u8_send( ETH_SPI_OP_RBM );
	
	while( length > 0 ){
		
		*data = spi_u8_send( 0 );
		data++;
		
		length--;
	}
	
	set_cs();
}

uint8_t eth_u8_read_buffer( void ){
	
	clear_cs();
	
	spi_u8_send( ETH_SPI_OP_RBM );
	
	uint8_t data = spi_u8_send( 0 );
	
	set_cs();
	
	return data;
}

void eth_v_write_buffer( uint8_t *data, uint16_t length ){
	
	clear_cs();
	
	spi_u8_send( ETH_SPI_OP_WBM );
	
	while( length > 0 ){
		
		spi_u8_send( *data );
		data++;
		
		length--;
	}
	
	set_cs();
}

void eth_v_write_phy( uint8_t addr, uint16_t data ){
	
	// set phy reg address
	eth_v_write_reg( MIREGADR, addr );
	
	// write data
	eth_v_write_reg( MIWRL, data & 0xff );
	eth_v_write_reg( MIWRH, data  >> 8 );
	
	// wait until the command completes
	while( ( eth_u8_read_reg( MISTAT ) & MISTAT_BUSY ) != 0 );
}

uint16_t eth_u16_read_phy( uint8_t addr ){

	// set phy reg address
	eth_v_write_reg( MIREGADR, addr );
	
	// set the phy read bit
	eth_v_set_bits( MICMD, MICMD_MIIRD );
	
	// wait until the command completes
	while( ( eth_u8_read_reg( MISTAT ) & MISTAT_BUSY ) != 0 );
	
	// clear the phy read bit
	eth_v_clear_bits( MICMD, MICMD_MIIRD );
	
	// read data
	uint16_t data = ( eth_u8_read_reg( MIRDH ) << 8 ) + eth_u8_read_reg( MIRDL );
	
	return data;
}

int8_t eth_i8_read_packet( void ){

	// set read pointer to next packet
	eth_v_write_reg( ERDPTL, next_packet_ptr & 0xff );
	eth_v_write_reg( ERDPTH, next_packet_ptr >> 8 );
	
	// read next packet pointer
	next_packet_ptr = eth_u8_read_buffer();
	next_packet_ptr |= eth_u8_read_buffer() << 8;
    
    uint16_t rx_packet_len;

	// read packet length
	rx_packet_len = eth_u8_read_buffer();
	rx_packet_len |= eth_u8_read_buffer() << 8;
	rx_packet_len -= ( sizeof(eth_hdr_t) + sizeof(uint32_t) ); // adjust for header length and frame check sequence
    
	// read status
	rx_status = eth_u8_read_buffer();
	rx_status |= eth_u8_read_buffer() << 8;
	
    // get header
    eth_hdr_t hdr;
    
    eth_v_read_buffer( (uint8_t *)&hdr, sizeof(hdr) );
    
    // create netmsg object
    netmsg_t netmsg = netmsg_nm_create( 0, rx_packet_len );

	if( netmsg >= 0 ){
		
        // data buffer
        uint8_t *data_ptr = netmsg_vp_get_data( netmsg );
        
        // read the packet
        eth_v_read_buffer( data_ptr, rx_packet_len );
        
        // call receive handler
        eth_v_receive_frame( hdr.type_length, netmsg );
	}
	
	// advance the read pointer
	eth_v_write_reg( ERXRDPTL, next_packet_ptr & 0xff );
	eth_v_write_reg( ERXRDPTH, next_packet_ptr >> 8 );
	
	// set the packet decrement bit to indicate we're done with the packet
	eth_v_set_bits( ECON2, ECON2_PKTDEC );
    
    //stats_v_increment( STAT_ETH_RECEIVED );

    // check if message was queued
    if( netmsg < 0 ){
            
        return -1;
    }

    return 0;
}

void eth_v_send_packet( eth_mac_addr_t *dest_addr, 
						 uint16_t type, 
						 uint8_t *data, 
						 uint16_t bufsize ){
	
	// set start of packet
	eth_v_write_reg( EWRPTL, ETH_TX_START & 0xff );
	eth_v_write_reg( EWRPTH, ETH_TX_START >> 8 );
	
	// set end of packet
	eth_v_write_reg( ETXNDL, ( ETH_TX_START + bufsize + sizeof(eth_hdr_t) ) & 0xff );
	eth_v_write_reg( ETXNDH, ( ETH_TX_START + bufsize + sizeof(eth_hdr_t) ) >> 8 );
	
	uint8_t control_byte = 0;
	
	// write the control byte (0) to the buffer
	eth_v_write_buffer( &control_byte, sizeof( control_byte ) );
	
	// build the header
	eth_hdr_t hdr;
	
	// set destination address
	hdr.dest_addr = *dest_addr;
	
	// set source address
	cfg_i8_get( CFG_PARAM_ETH_MAC_ADDRESS, &hdr.source_addr );

	// set type
	hdr.type_length = type;
	
	// write the header to the buffer
	eth_v_write_buffer( (uint8_t*)&hdr, sizeof( hdr ) );
	
	// write the packet to the buffer
	eth_v_write_buffer( data, bufsize );
	
	// clear the irq flag
	eth_v_clear_bits( EIR, EIR_TXIF );
	
	// send the packet
	eth_v_set_bits( ECON1, ECON1_TXRTS );	
	
	// set transmit timestamp
	tx_timestamp = tmr_u32_get_system_time();
	
	// set tx busy
	tx_busy = TRUE;
}


PT_THREAD( eth_tx_monitor_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
	while(1){
		
		THREAD_WAIT_WHILE( pt, tx_busy == FALSE );
		
		THREAD_WAIT_WHILE( pt, ( tmr_i8_compare_time( tx_timestamp + 1000 ) > 0 ) &&
							   ( tx_busy == TRUE ) );
						   
		
		if( tx_busy == TRUE ){
			
            //stats_v_increment( STAT_ETH_TRANSMIT_FAILURES );
			
			low_level_init();
			
			tx_busy = FALSE;
		}
	}
	
PT_END( pt );
}

// (C)2009-2012 by Jeremy Billheimer

