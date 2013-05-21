
#ifndef _ENC28J60_H
#define _ENC28J60_H

#include "netmsg.h"
#include "threading.h"

#define SIGNAL_ETH_IRQ      SIGNAL_APP_0


// pin connections
/*
 * rev 4.3 and below:
 */
#define ETH_IRQ_PORT PORTE
#define ETH_IRQ_DDR DDRE
#define ETH_IRQ_PIN 4

#define ETH_RST_PORT PORTE
#define ETH_RST_DDR DDRE
#define ETH_RST_PIN 5

#define ETH_CS_PORT PORTE
#define ETH_CS_DDR DDRE
#define ETH_CS_PIN 3

// eth irq vector setup
#define ETH_IRQ_VECTOR INT4_vect

#define ETH_IRQ_CTRL_REG EICRB
#define ETH_IRQ_CTRL_BITS ( ( 1 << ( ISC41 ) ) | ( 0 << ( ISC40 ) ) ) // falling edge
#define ETH_IRQ_MSK_BITS ( 1 << INT4 )

/*
 * rev 4.4 and up:
 */
#define ETH_4_4_IRQ_PORT PORTD
#define ETH_4_4_IRQ_DDR DDRD
#define ETH_4_4_IRQ_PIN 0

#define ETH_4_4_RST_PORT PORTG
#define ETH_4_4_RST_DDR DDRG
#define ETH_4_4_RST_PIN 2

#define ETH_4_4_CS_PORT PORTG
#define ETH_4_4_CS_DDR DDRG
#define ETH_4_4_CS_PIN 5

// eth irq vector setup
#define ETH_4_4_IRQ_VECTOR INT0_vect

#define ETH_4_4_IRQ_CTRL_REG EICRA
#define ETH_4_4_IRQ_CTRL_BITS ( ( 1 << ( ISC01 ) ) | ( 0 << ( ISC00 ) ) ) // falling edge
#define ETH_4_4_IRQ_MSK_BITS ( 1 << INT0 )


// this defines the RX buffer size on the Ethernet controller
#define ETH_ONCHIP_RX_BUFFER_SIZE 4096

#define ETH_MAX_FRAME_LEN 1518

#define ETH_BUFFER_SIZE 8192 // DO NOT CHANGE!!!
#define ETH_RX_START 0
#define ETH_RX_END ( ETH_ONCHIP_RX_BUFFER_SIZE - 1 )
#define ETH_TX_START ETH_ONCHIP_RX_BUFFER_SIZE
#define ETH_TX_END ( ETH_BUFFER_SIZE - 1 )


// ethertypes (byte swapped!)
#define ETH_TYPE_IPv4 0x0008
#define ETH_TYPE_ARP  0x0608


typedef struct{
	uint8_t mac[6];
} eth_mac_addr_t;

typedef struct{
	eth_mac_addr_t dest_addr;
	eth_mac_addr_t source_addr;
	uint16_t type_length;
	// data follows
} eth_hdr_t;


// SPI opcodes:
#define ETH_SPI_OP_RCR 0b00000000 // read control register
#define ETH_SPI_OP_RBM 0b00111010 // read buffer memory
#define ETH_SPI_OP_WCR 0b01000000 // write control register
#define ETH_SPI_OP_WBM 0b01111010 // write buffer memory
#define ETH_SPI_OP_BFS 0b10000000 // bit field set
#define ETH_SPI_OP_BFC 0b10100000 // bit field clear
#define ETH_SPI_OP_SRC 0b11111111 // system soft reset


// Control register addresses

// bits 0-4 address
// bits 5-6 bank
// bit 7 set MAC/PHY register (requires dummy read/write)

#define ADDR_MSK 0b00011111

#define BANK0 0b00000000
#define BANK1 0b00100000
#define BANK2 0b01000000
#define BANK3 0b01100000

#define BANK_MSK 0b01100000

#define MAC_PHY 0b10000000


// These registers are common to all banks:
#define EIE		 	0X1B
#define EIR		 	0X1C
#define ESTAT		0X1D

#define ECON2		0X1E
#define ECON1		0X1F

// BANK 0
#define ERDPTL		(0x00 | BANK0)
#define ERDPTH		(0x01 | BANK0)
#define EWRPTL		(0x02 | BANK0)
#define EWRPTH		(0x03 | BANK0)
#define ETXSTL		(0x04 | BANK0)
#define ETXSTH		(0x05 | BANK0)
#define ETXNDL		(0x06 | BANK0)
#define ETXNDH		(0x07 | BANK0)
#define ERXSTL		(0x08 | BANK0)
#define ERXSTH		(0x09 | BANK0)
#define ERXNDL		(0x0A | BANK0)
#define ERXNDH		(0x0B | BANK0)
#define ERXRDPTL	(0x0C | BANK0)
#define ERXRDPTH	(0x0D | BANK0)
#define ERXWRPTL	(0x0E | BANK0)
#define ERXWRPTH	(0x0F | BANK0)
#define EDMASTL		(0x10 | BANK0)
#define EDMASTH		(0x11 | BANK0)
#define EDMANDL		(0x12 | BANK0)
#define EDMANDH		(0x13 | BANK0)
#define EDMADSTL	(0x14 | BANK0)
#define EDMADSTH	(0x15 | BANK0)
#define EDMACSL 	(0x16 | BANK0)
#define EDMACSH 	(0x17 | BANK0)


// BANK 1
#define ETH0		(0X00 | BANK1)
#define ETH1		(0X01 | BANK1)
#define ETH2		(0X02 | BANK1)
#define ETH3		(0X03 | BANK1)
#define ETH4		(0X04 | BANK1)
#define ETH5		(0X05 | BANK1)
#define ETH6		(0X06 | BANK1)
#define ETH7		(0X07 | BANK1)
#define EPMM0		(0X08 | BANK1)
#define EPMM1		(0X09 | BANK1)
#define EPMM2		(0X0A | BANK1)
#define EPMM3		(0X0B | BANK1)
#define EPMM4		(0X0C | BANK1)
#define EPMM5		(0X0D | BANK1)
#define EPMM6		(0X0E | BANK1)
#define EPMM7		(0X0F | BANK1)
#define EPMCSL		(0X10 | BANK1)
#define EPMCSH		(0X11 | BANK1)

#define EPMOL		(0X14 | BANK1)
#define EPMOH		(0X15 | BANK1)

#define ERXFCON		(0X18 | BANK1)
#define EPKTCNT		(0X19 | BANK1)


// BANK 2
#define MACON1		(0X00 | BANK2 | MAC_PHY )

#define MACON3		(0X02 | BANK2 | MAC_PHY )
#define MACON4		(0X03 | BANK2 | MAC_PHY )
#define MABBIPG		(0X04 | BANK2 | MAC_PHY )

#define MAIPGL		(0X06 | BANK2 | MAC_PHY )
#define MAIPGH		(0X07 | BANK2 | MAC_PHY )
#define MACLCON1	(0X08 | BANK2 | MAC_PHY )
#define MACLCON2	(0X09 | BANK2 | MAC_PHY )
#define MAMXFLL		(0X0A | BANK2 | MAC_PHY )
#define MAMXFLH		(0X0B | BANK2 | MAC_PHY )

#define MICMD		(0X12 | BANK2 | MAC_PHY )
 
#define MIREGADR	(0X14 | BANK2 | MAC_PHY )

#define MIWRL		(0X16 | BANK2 | MAC_PHY )
#define MIWRH		(0X17 | BANK2 | MAC_PHY )
#define MIRDL		(0X18 | BANK2 | MAC_PHY )
#define MIRDH		(0X19 | BANK2 | MAC_PHY )


// BANK 3
#define MAADR5		(0X00 | BANK3 | MAC_PHY )
#define MAADR6		(0X01 | BANK3 | MAC_PHY )
#define MAADR3		(0X02 | BANK3 | MAC_PHY )
#define MAADR4		(0X03 | BANK3 | MAC_PHY )
#define MAADR1		(0X04 | BANK3 | MAC_PHY )
#define MAADR2		(0X05 | BANK3 | MAC_PHY )
#define EBSTSD		(0X06 | BANK3)
#define EBSTCON		(0X07 | BANK3)
#define EBSTCSL		(0X08 | BANK3)
#define EBSTCSH		(0X09 | BANK3)
#define MISTAT		(0X0A | BANK3 | MAC_PHY )

#define EREVID		(0X12 | BANK3)

#define ECOCON		(0X15 | BANK3)

#define EFLOCON		(0X17 | BANK3)
#define EPAUSL		(0X18 | BANK3)
#define EPAUSH		(0X19 | BANK3)


// Register bit names
// EIE
#define EIE_INTIE		0b10000000
#define EIE_PKTIE		0b01000000
#define EIE_DMAIE		0b00100000
#define EIE_LINKIE		0b00010000
#define EIE_TXIE		0b00001000
#define EIE_TXERIE		0b00000010
#define EIE_RXERIE		0b00000001

// EIR
#define EIR_PKTIF		0b01000000
#define EIR_DMAIF		0b00100000
#define EIR_LINKIF		0b00010000
#define EIR_TXIF		0b00001000
#define EIR_TXERIF		0b00000010
#define EIR_RXERIF		0b00000001

// ESTAT
#define ESTAT_INT		0b10000000
#define ESTAT_BUFER		0b01000000

#define ESTAT_LATECOL	0b00010000

#define ESTAT_RXBUSY	0b00000100
#define ESTAT_TXABRT	0b00000010
#define ESTAT_CLKRDY	0b00000001

// ECON2
#define ECON2_AUTOINC	0b10000000
#define ECON2_PKTDEC	0b01000000
#define ECON2_PWRSV		0b00100000

#define ECON2_VRPS		0b00001000

// ECON1
#define ECON1_TXRST		0b10000000
#define ECON1_RXRST		0b01000000
#define ECON1_DMAST		0b00100000
#define ECON1_CSUMEN	0b00010000
#define ECON1_TXRTS		0b00001000
#define ECON1_RXEN		0b00000100
#define ECON1_BSEL1		0b00000010
#define ECON1_BSEL0		0b00000001

// ERXFCON
#define ERXFCON_UCEN	0b10000000
#define ERXFCON_ANDOR	0b01000000
#define ERXFCON_CRCEN	0b00100000
#define ERXFCON_PMEN	0b00010000
#define ERXFCON_MPEN	0b00001000
#define ERXFCON_HTEN	0b00000100
#define ERXFCON_MCEN	0b00000010
#define ERXFCON_BCEN	0b00000001

// MACON1
#define MACON1_TXPAUS	0b00001000
#define MACON1_RXPAUS	0b00000100
#define MACON1_PASSALL	0b00000010
#define MACON1_MARXEN	0b00000001

// MACON3
#define MACON3_PADCFG2	0b10000000
#define MACON3_PADCFG1	0b01000000
#define MACON3_PADCFG0	0b00100000
#define MACON3_TXCRCEN	0b00010000
#define MACON3_PHDREN	0b00001000
#define MACON3_HFRMEN	0b00000100
#define MACON3_FRMLNEN	0b00000010
#define MACON3_FULDPX	0b00000001

// MACON4
#define MACON4_DEFER	0b01000000
#define MACON4_BPEN		0b00100000
#define MACON4_NOBKOFF	0b00010000

// MICMD
#define MICMD_MIISCAN	0b00000010
#define MICMD_MIIRD		0b00000001

// EBSTCON
#define EBSTCON_PSV2	0b10000000
#define EBSTCON_PSV1	0b01000000
#define EBSTCON_PSV0	0b00100000
#define EBSTCON_PSEL	0b00010000
#define EBSTCON_TMSEL1	0b00001000
#define EBSTCON_TMSEL0	0b00000100
#define EBSTCON_TME		0b00000010
#define EBSTCON_BISTST	0b00000001

// MISTAT
#define MISTAT_NVALID	0b00000100
#define MISTAT_SCAN		0b00000010
#define MISTAT_BUSY		0b00000001

// ECOCON
#define ECOCON_COCON2	0b00000100
#define ECOCON_COCON1	0b00000010
#define ECOCON_COCON0	0b00000001

// EFLOCON
#define EFLOCON_FULDPXS	0b00000100
#define EFLOCON_FCEN1	0b00000010
#define EFLOCON_FCEN0	0b00000001


// PHY registers
#define PHCON1		0x00
#define PHSTAT1		0x01
#define PHID1		0x02
#define PHID2		0x03
#define PHCON2		0x10
#define PHSTAT2		0x11
#define PHIE		0x12
#define PHIR		0x13
#define PHLCON		0x14

// PHY register bit names
// PHCON1
#define PHCON1_PRST		0b1000000000000000
#define PHCON1_PLOOPBK	0b0100000000000000
#define PHCON1_PPWRSV	0b0000100000000000
#define PHCON1_PDPXMD	0b0000000100000000

// PHSTAT1
#define PHSTAT1_PFDPX	0b0001000000000000
#define PHSTAT1_PHDPX	0b0000100000000000
#define PHSTAT1_LLSTAT	0b0000000000000100
#define PHSTAT1_JBSTAT	0b0000000000000010

// PHCON2
#define PHCON2_FRCLNK	0b0100000000000000
#define PHCON2_TXDIS	0b0010000000000000
#define PHCON2_JABBER	0b0000010000000000
#define PHCON2_HDLDIS	0b0000000100000000

// PHSTAT2
#define PHSTAT2_TXSTAT	0b0010000000000000
#define PHSTAT2_RXSTAT	0b0001000000000000
#define PHSTAT2_COLSTAT	0b0000100000000000
#define PHSTAT2_LSTAT	0b0000010000000000
#define PHSTAT2_DPXSTAT	0b0000001000000000
#define PHSTAT2_PLRITY	0b0000000001000000

// PHIE
#define PHIE_PLNKIE		0b0000000000010000
#define PHIE_PGEIE		0b0000000000000010

// PHIR
#define PHIR_PLNKIF		0b0000000000010000
#define PHIR_PGIF		0b0000000000000100

// PHLCON
#define PHLCON_LACFG_MSK 0b0000111100000000
#define PHLCON_LBCFG_MSK 0b0000000011110000
#define PHLCON_LFRQ_MSK  0b0000000000001100
#define PHLCON_STRCH	 0b0000000000000010


extern void ( *eth_v_receive_frame )( uint16_t type_len, netmsg_t netmsg );

void eth_v_io_init( void );
uint8_t eth_u8_init( void );
bool eth_b_enabled( void );

bool eth_b_tx_busy( void );

void eth_v_set_bank( uint8_t addr );
void eth_v_write_reg( uint8_t addr, uint8_t data );
uint8_t eth_u8_read_reg( uint8_t addr );
void eth_v_set_bits( uint8_t addr, uint8_t bits );
void eth_v_clear_bits( uint8_t addr, uint8_t bits );
void eth_v_reset( void );
void eth_v_read_buffer( uint8_t *data, uint16_t length );
uint8_t eth_u8_read_buffer( void );
void eth_v_write_buffer( uint8_t *data, uint16_t length );
void eth_v_write_phy( uint8_t addr, uint16_t data );
uint16_t eth_u16_read_phy( uint8_t addr );

int8_t eth_i8_read_packet( void );

void eth_v_send_packet( eth_mac_addr_t *dest_addr, 
						 uint16_t type, 
						 uint8_t *data, 
						 uint16_t bufsize );

#endif

