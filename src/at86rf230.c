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


#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#include "target.h"

#include "system.h"

#include "config.h"
#include "netmsg.h"
#include "timers.h"
#include "threading.h"
#include "statistics.h"

#include "spi.h"
#include "netmsg_handlers.h"
#include "at86rf230.h"
#include "io.h"

#include <string.h>
#include <stdio.h>


static rf_mode_t8 rf_mode;

static rf_tx_status_t8 tx_status;

static rx_frame_buf_t rx_buffer[RF_RX_BUFFER_SIZE];
static volatile uint8_t rx_frames;
static volatile uint8_t rx_ins;
static volatile uint8_t rx_ext;

static uint8_t lqi;


// local functions:
PT_THREAD( receive_thread( pt_t *pt, void *state ) );
PT_THREAD( rf_pll_schedule_thread( pt_t *pt, void *state ) );


uint8_t rf_u8_init( void ){

	TRXPR = 1; // force a reset

	_delay_ms( 5 );
    
    // check hardware revision
    if( io_u8_get_board_rev() == 0 ){
    
        // disable internal voltage regulators (1.8v rev 4 boards only).  This will not be necessary on rev 4.1.
        VREG_CTRL = ( 1 << AVREG_EXT ) | ( 1 << DVREG_EXT );
        // note the datasheet poorly documents this register.
    }

	rf_v_set_internal_state( CMD_PLL_ON ); // set mode to PLL_ON

	// set cca mode
	//rf_v_set_cca_mode( MODE_3_THRESH_CS );
	//rf_v_set_cca_mode( MODE_2_CS );
	//rf_v_set_cca_mode( MODE_1_THRESH );
	uint16_t cca_mode;
    cfg_i8_get( CFG_PARAM_WCOM_CCA_MODE, &cca_mode );
    
    // bounds check mode
    if( ( cca_mode < 1 ) || ( cca_mode > 3 ) ){
        
        cca_mode = MODE_2_CS;
        cfg_v_set( CFG_PARAM_WCOM_CCA_MODE, &cca_mode );
    }
    
    // set cca mode
    rf_v_set_cca_mode( cca_mode );

    // get cca threshold
    uint16_t cca_thresh;
    cfg_i8_get( CFG_PARAM_WCOM_CCA_THRESHOLD, &cca_thresh );
    
    // bounds check mode
    if( cca_thresh >= 16 ){
        
        cca_thresh = 8;
        cfg_v_set( CFG_PARAM_WCOM_CCA_THRESHOLD, &cca_thresh );
    }
    
	rf_v_set_cca_ed_threshold( cca_thresh );
	
    // set back off exponents
    uint16_t min_be;
    uint16_t max_be;
    cfg_i8_get( CFG_PARAM_WCOM_MIN_BE, &min_be );
    cfg_i8_get( CFG_PARAM_WCOM_MAX_BE, &max_be );
    
    // bounds check
    if( max_be < 3 ){
        
        max_be = 3;
        cfg_v_set( CFG_PARAM_WCOM_MAX_BE, &max_be );
    }
    else if( max_be > 8 ){
        
        max_be = 8;
        cfg_v_set( CFG_PARAM_WCOM_MAX_BE, &max_be );
    }

    if( min_be > max_be ){
        
        min_be = max_be;
        cfg_v_set( CFG_PARAM_WCOM_MIN_BE, &min_be );
    }

    rf_v_set_be( min_be, max_be );

    // set receiver sensitivity
    uint16_t rx_sens;
    cfg_i8_get( CFG_PARAM_WCOM_RX_SENSITIVITY, &rx_sens );
    
    // bounds check
    if( rx_sens > 15 ){
        
        rx_sens = 15;
        cfg_v_set( CFG_PARAM_WCOM_RX_SENSITIVITY, &rx_sens );
    }
    
    rf_v_set_rx_sensitivity( rx_sens );

    // set power
    uint16_t tx_power;
    cfg_i8_get( CFG_PARAM_WCOM_MAX_TX_POWER, &tx_power );
    
    if( tx_power > 15 ){
        
        // note 15 is the lowest setting, 0 is the highest

        tx_power = 15;
        cfg_v_set( CFG_PARAM_WCOM_MAX_TX_POWER, &tx_power );
    }

    rf_v_set_power( tx_power ); 
	
    uint16_t csma_tries;
    uint16_t tx_tries;
    cfg_i8_get( CFG_PARAM_WCOM_CSMA_TRIES, &csma_tries );
    cfg_i8_get( CFG_PARAM_WCOM_TX_HW_TRIES, &tx_tries );
    
    if( csma_tries > 5 ){
        
        csma_tries = 5;
        cfg_v_set( CFG_PARAM_WCOM_CSMA_TRIES, &csma_tries );
    }
    
    if( tx_tries > 15 ){
        
        tx_tries = 15;
        cfg_v_set( CFG_PARAM_WCOM_TX_HW_TRIES, &tx_tries );
    }

	// set up CSMA and frame retries
    rf_v_set_csma_tries( csma_tries );
    rf_v_set_frame_retries( tx_tries );
   
	// park on channel 11
	rf_v_set_channel( 11 );
	
    // enable interrupts
    IRQ_MASK |= _BV( TX_END_EN ) |
                _BV( RX_END_EN );
        
    
    // set mode to normal
    // this will configure address and pan id
    rf_v_set_mode( RF_MODE_NORMAL );

    // create threads
	thread_t_create( receive_thread,
                     PSTR("rf_receive"),
                     0,
                     0 );

    thread_t_create( rf_pll_schedule_thread,
                     PSTR("rf_pll_calibration"),
                     0,
                     0 );

    // set up CSMA seed
    CSMA_SEED_0 = rf_u8_get_random();
    CSMA_SEED_1 |= rf_u8_get_random() & 0x07;

	return 0;
}


void rf_v_set_mode( rf_mode_t8 mode ){
   
	// set up address and pan id
	uint64_t long_address;
	uint16_t pan_id;
	uint16_t short_address;
	
    rf_mode = mode;

    if( ( mode == RF_MODE_NORMAL ) ||
        ( mode == RF_MODE_TX_ONLY ) ){
    
        // get address and pan id from config database
        cfg_i8_get( CFG_PARAM_802_15_4_MAC_ADDRESS, &long_address );
        cfg_i8_get( CFG_PARAM_802_15_4_PAN_ID, &pan_id );
        cfg_i8_get( CFG_PARAM_802_15_4_SHORT_ADDRESS, &short_address );

        // disable promiscuous mode
        XAH_CTRL_1 &= ~( 1 << AACK_PROM_MODE );

        // enable auto ack
        CSMA_SEED_1 &= ~( 1 << AACK_DIS_ACK );
        
        // check if normal mode, enable receiver
        if( mode == RF_MODE_NORMAL ){

            // enter rx auto ack
            rf_v_set_internal_state( CMD_RX_AACK_ON );
        }
        else{
            
            // enter pll on
            rf_v_set_internal_state( CMD_PLL_ON );
        }
    }
    else if( mode == RF_MODE_PROMISCUOUS ){
        
        long_address = 0;
        pan_id = 0;
        short_address = 0;

        // enable promiscuous mode
        XAH_CTRL_1 |= ( 1 << AACK_PROM_MODE );

        // disable auto ack
        CSMA_SEED_1 |= ( 1 << AACK_DIS_ACK );
    
        // enter rx basic mde
        rf_v_set_internal_state( CMD_RX_ON );
    }
    else{
        
        ASSERT( 0 );
    }

    // set addresses
    rf_v_set_pan_id( pan_id );
	rf_v_set_long_addr( long_address );
    rf_v_set_short_addr( short_address );
}

rf_mode_t8 rf_u8_get_mode( void ){
    
    return rf_mode;
}

void rf_v_set_auto_ack_time( uint8_t time ){
	
	uint8_t temp = XAH_CTRL_1 & ~RF_AACK_TIME_MSK;
	
	XAH_CTRL_1 = temp | ( time & RF_AACK_TIME_MSK );
}


void rf_v_set_rx_sensitivity( uint8_t sens ){
		
	uint8_t temp = RX_SYN & ~RF_RX_SENS_MSK;
	
	RX_SYN = temp | ( sens & RF_RX_SENS_MSK );
}

void rf_v_set_data_rate( uint8_t rate ){

	uint8_t temp = TRX_CTRL_2 & ~RF_DATA_RATE_MSK;
	
	TRX_CTRL_2 = temp | ( rate & RF_DATA_RATE_MSK );
}

	
void rf_v_set_internal_state( uint8_t mode ){
	
    TRX_STATE = mode & TRX_CMD_MSK;
}

uint8_t rf_u8_get_internal_state( void ){
	
	return TRX_STATUS & TRX_STATUS_MSK;
}

uint8_t rf_u8_get_tx_status( void ){

	return ( TRX_STATE & TRAC_STATUS_MSK ) >> TRAC_STATUS_SHFT;
}

void rf_v_set_power( uint8_t power ){
	
	uint8_t temp = PHY_TX_PWR & ~TX_PWR_MSK;

	PHY_TX_PWR =  temp | ( power & TX_PWR_MSK );
}

void rf_v_set_channel( uint8_t channel ){
	
	ASSERT( channel >= RF_LOWEST_CHANNEL );
	ASSERT( channel <= RF_HIGHEST_CHANNEL );

	uint8_t temp = PHY_CC_CCA;
	
	// reset the channel bits to 0s
	temp &= ~CHANNEL_MSK;
	
	// set the channel bits
	temp |= ( channel & CHANNEL_MSK );
	
	// load the register
	PHY_CC_CCA = temp;
}

uint8_t rf_u8_get_channel( void ){
	
	return PHY_CC_CCA & CHANNEL_MSK;
}

uint8_t rf_u8_get_part_num( void ){
	
	return PART_NUM;
}

uint8_t rf_u8_get_man_id0( void ){
	
	return MAN_ID_0;
}

uint8_t rf_u8_get_man_id1( void ){
	
	return MAN_ID_1;
}

uint8_t rf_u8_get_version_num( void ){
    
	return VERSION_NUM;
}


void rf_v_set_short_addr( uint16_t addr ){
    
    SHORT_ADDR_0 = addr;
	SHORT_ADDR_1 = addr >> 8;
}

void rf_v_set_long_addr( uint64_t long_address ){

    IEEE_ADDR_0 = long_address;
    IEEE_ADDR_1 = long_address >> 8;
    IEEE_ADDR_2 = long_address >> 16;
    IEEE_ADDR_3 = long_address >> 24;
    IEEE_ADDR_4 = long_address >> 32;
    IEEE_ADDR_5 = long_address >> 40;
    IEEE_ADDR_6 = long_address >> 48;
    IEEE_ADDR_7 = long_address >> 56;
}

void rf_v_set_pan_id( uint16_t pan_id ){

    PAN_ID_0 = pan_id;
    PAN_ID_1 = pan_id >> 8; 
}

uint8_t rf_u8_get_random( void ){
    
    uint8_t temp = 0;
    
    for( uint8_t bits = 0; bits < 8; bits += 2 ){
		
        temp |= ( ( ( PHY_RSSI & RND_MSK ) >> RND_SHFT ) << bits );
		
		_delay_us( 1 ); // wait 1 us between register reads
	}
    
    return temp;
}


void rf_v_set_cca_mode( uint8_t mode ){
	
	ASSERT( ( mode != 0 ) && ( mode <= 3 ) );
	
	uint8_t temp = PHY_CC_CCA;
	
	// mask off cca mode bits
	temp &= ~CCA_MODE_MSK;
	
	// set new cca mode bits
	temp |= ( mode << CCA_MODE_SHFT );
	
	// write new setting
	PHY_CC_CCA = temp;
}

void rf_v_set_cca_ed_threshold( uint8_t threshold ){
	
	ASSERT( threshold < 16 );
	
	uint8_t temp = CCA_THRES & ~CCA_ED_THRES_MSK;
	
	temp |= ( threshold & CCA_ED_THRES_MSK );
	
	CCA_THRES = temp;
}

void rf_v_set_be( uint8_t min_be, uint8_t max_be ){
    
    CSMA_BE = ( ( max_be & 0x0f ) << 4 ) | ( min_be & 0x0f );
}

void rf_v_set_csma_tries( uint8_t tries ){

    uint8_t temp = XAH_CTRL_0;
    
    temp &= ~XAH_CSMA_TRIES_MSK;
    temp |= ( tries << XAH_CSMA_TRIES_SHFT );

	XAH_CTRL_0 = temp;
}

void rf_v_set_frame_retries( uint8_t tries ){
    
	uint8_t temp = XAH_CTRL_0;
    
    temp &= ~XAH_FRAME_TRIES_MSK;
    temp |= ( tries << XAH_FRAME_TRIES_SHFT );

	XAH_CTRL_0 = temp;
}

void rf_v_calibrate_pll( void ){
	
	uint8_t temp = PLL_CF;
	
	temp |= 0x80;
	
	PLL_CF = temp;
}	


void rf_v_calibrate_delay( void ){

	uint8_t temp = PLL_DCU;
	
	temp |= 0x80;

	PLL_DCU = temp;
}


// transmit function
void rf_v_transmit( void ){
	
    stats_v_increment( STAT_RF_TRANSMIT_REQUESTS );
    
    // initiate transmission
	TRX_STATE = CMD_TX_START;	
}

// requests transmit mode.
// returns -1 if unable to switch modes (receiving or busy)
int8_t rf_i8_request_transmit_mode( rf_tx_mode_t8 mode ){
    
    if( tx_status == RF_TRANSMIT_STATUS_BUSY ){
        
        return -1;
    }

    // check if receiving
    if( rf_u8_get_internal_state() == BUSY_RX_AACK ){
        
        return -1;
    }
    
    // request pll on mode
    rf_v_set_internal_state( CMD_FORCE_PLL_ON );
    
    if( mode == RF_TX_MODE_AUTO_RETRY ){

        // request tx auto retry
        rf_v_set_internal_state( TX_ARET_ON );
        
        // check mode
        if( rf_u8_get_internal_state() != TX_ARET_ON ){
            
            stats_v_increment( STAT_DEBUG_1 );

            return -1;
        }
    }
    
    // set status to busy
    tx_status = RF_TRANSMIT_STATUS_BUSY;

    return 0;
}


// Frame buffer access functions:

void rf_v_write_frame_buf( uint8_t length, const uint8_t *data ){
	
	// tx buffer can only hold 127 bytes
	ASSERT( length <= RF_MAX_FRAME_SIZE );
	
	uint8_t *reg = (uint8_t *)&TRXFBST;
	
	*reg = length;
	
	reg++;
	
	memcpy( reg, data, length );
}

// read data from frame buffer to *ptr.
// returns number of bytes read.
uint8_t rf_u8_read_frame_buf( uint8_t max_length, uint8_t *ptr ){

	uint8_t bytes_to_read;
	uint8_t frame_length;
	
	uint8_t *reg = (uint8_t *)&TST_RX_LENGTH;
	
	frame_length = *reg;
	
	// if the frame to receive is larger than the given buffer,
	// limit the number of bytes read
	if( frame_length > max_length ){
		
		bytes_to_read = max_length;
	}
	else{
		
		bytes_to_read = frame_length;
	}
	
	reg = (uint8_t *)&TRXFBST;
	
	memcpy( ptr, reg, bytes_to_read );

    lqi = *( reg + frame_length );
	
	// return number of bytes read
	return bytes_to_read;
}

// transceiver transmit end interrupt
ISR(TRX24_TX_END_vect){
	
    stats_v_increment( STAT_RF_TX_INTERRUPTS );
	
	// get mode
	uint8_t mode = rf_u8_get_internal_state();
	
	if( ( mode == RX_AACK_ON ) || ( mode == BUSY_RX_AACK ) ){
		
		// sent acknowledgement
		stats_v_increment( STAT_RF_ACK_SENT );
	}
	else{
		
        // transmit complete
		
		uint8_t status = rf_u8_get_tx_status();
		
		if( status == TRAC_STATUS_SUCCESS ){
			
			stats_v_increment( STAT_RF_SENT );

            tx_status = RF_TRANSMIT_STATUS_OK;
		}
        else if( status == TRAC_STATUS_CHANNEL_ACCESS_FAILURE ){

            tx_status = RF_TRANSMIT_STATUS_FAILED_CCA;
        }
        else{

            tx_status = RF_TRANSMIT_STATUS_FAILED_NO_ACK;
        }

		// set mode back to pll on
		rf_v_set_internal_state( CMD_PLL_ON );
	    
        // check mode
        if( rf_mode != RF_MODE_TX_ONLY ){

            // set mode to rx auto ack
            rf_v_set_internal_state( CMD_RX_AACK_ON );
        }
	}
}


PT_THREAD( receive_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );
	
    while(1){
        
        // wait for a received frame
		THREAD_WAIT_SIGNAL( pt, SIGNAL_RF_RECEIVE );
        
        // clear received frame flag
        ATOMIC;
        
        uint8_t frames_to_process = rx_frames;
        rx_frames = 0;

        END_ATOMIC;
        
        // process all received buffers
        while( frames_to_process > 0 ){
            
            frames_to_process--;
            
            // send receive data to netmsg
            netmsg_v_receive_802_15_4_raw( &rx_buffer[rx_ext] );

            // clear buffer
            ATOMIC;
            rx_buffer[rx_ext].len = 0;
            END_ATOMIC;
            
            // advance pointer
            rx_ext++;

            if( rx_ext >= cnt_of_array(rx_buffer) ){
                
                rx_ext = 0;
            }
        }
	}
	
PT_END( pt );
}


// transceiver receive end interrupt
ISR(TRX24_RX_END_vect){
    
    uint32_t timestamp = tmr_u32_get_system_time_us();

    stats_v_increment( STAT_RF_RX_INTERRUPTS );
    
    // check if there is an available buffer
    if( ( rx_buffer[rx_ins].len == 0 ) &&
        ( rx_frames < cnt_of_array(rx_buffer) ) ){
        
        // copy data to frame buffer
        rx_buffer[rx_ins].timestamp = timestamp;
        rx_buffer[rx_ins].len = rf_u8_read_frame_buf( sizeof(rx_buffer[rx_ins].data), rx_buffer[rx_ins].data );
        rx_buffer[rx_ins].lqi = lqi;
        rx_buffer[rx_ins].ed = PHY_ED_LEVEL;
        
        // advance pointer and increment frame count
        rx_frames++;
        rx_ins++;
        
        if( rx_ins >= cnt_of_array(rx_buffer) ){
            
            rx_ins = 0;
        }

        thread_v_signal( SIGNAL_RF_RECEIVE );
    }
    else{
        
        // receive overrun
        stats_v_increment( STAT_RF_RECEIVE_OVERRUNS );	
    }
}

// manual energy detection done
ISR(TRX24_CCA_ED_DONE_vect){
	/*
	// read ed
	uint8_t ed = PHY_ED_LEVEL;
	
	// set channel back to the parking channel
	rf_v_set_channel( parking_channel );

	// set mode back to RX auto ack
	rf_v_set_internal_state( PLL_ON );
	rf_v_set_internal_state( CMD_RX_AACK_ON );
	
	// while the radio is switching channels and modes, average in the ED value
	uint16_t temp = channel_energy[scan_channel - RF_LOWEST_CHANNEL];
	
	temp *= RF_SCAN_SAMPLES_PER_CHANNEL;
	temp += ed;
	temp /= ( RF_SCAN_SAMPLES_PER_CHANNEL + 1 );
	
	channel_energy[scan_channel - RF_LOWEST_CHANNEL] = temp;
	
	stats_v_increment( STAT_RF_CCA_INTERRUPTS );
    */
}


PT_THREAD( rf_pll_schedule_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
	while(1){
		
		static uint32_t time_wait;
		
		time_wait = RF_PLL_CAL_INTERVAL;
		
		TMR_WAIT( pt, time_wait );
		
        // calibrate pll
        rf_v_calibrate_pll();
        
        // calibrate delay
        rf_v_calibrate_delay();
    }
	
PT_END( pt );
}


rf_tx_status_t8 rf_u8_get_transmit_status( void ){
    
    // check if receiving
    if( rf_u8_get_internal_state() == BUSY_RX_AACK ){
        
        return RF_TRANSMIT_STATUS_BUSY;
    }

    return tx_status;
}

void rf_v_sleep( void ){
    
    rf_v_set_internal_state( CMD_FORCE_TRX_OFF );
    TRXPR |= ( 1 << SLPTR ); 

    rf_mode = RF_MODE_SLEEP;
}

void rf_v_wakeup( void ){

    TRXPR &= ~( 1 << SLPTR ); 

    // enter pll on
    rf_v_set_internal_state( CMD_PLL_ON );

    rf_v_set_mode( RF_MODE_NORMAL );
}

bool rf_b_is_sleeping( void ){
    
    return rf_mode == RF_MODE_SLEEP;
}
