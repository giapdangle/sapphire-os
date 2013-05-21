
#ifndef _ARP_H
#define _ARP_H

#include "target.h"

#include "ip.h"
#include "enc28j60.h"

#define ARP_MAX_CACHE_ENTRIES 16
#define ARP_AGING_TICK_MS 100
#define ARP_MAX_AGE_TICKS 100

// IPv4 over ARP packet
typedef struct{
	uint16_t htype; // hardware type
	uint16_t ptype; // protocol type
	uint8_t hlen; // hardware address length
	uint8_t plen; // protocol address length
	uint16_t oper; // operation
	uint8_t sha[6]; // sender hardware address
	uint8_t spa[4]; // sender protocol address
	uint8_t tha[6]; // target hardware address
	uint8_t tpa[4]; // target protocol address
} arp_t;

// hardware type (byte swapped)
#define ARP_HTYPE_ETHERNET 0x0100

// protocol type (byte swapped)
#define ARP_PTYPE_IPv4 0x0008

// ethernet hardware length
#define ARP_HLEN_ETH 6

// protocol length
#define ARP_PLEN_ETH 4

// operation (bytes swapped!)
#define ARP_OPER_REQUEST 0x0100
#define ARP_OPER_REPLY 0x0200


typedef struct{
	eth_mac_addr_t eth_addr;
	ip_addr_t ip_addr;
	uint8_t age;
} arp_entry_t;



void arp_v_init( void );

bool arp_b_get_address( ip_addr_t *ip, eth_mac_addr_t *eth_mac );

void arp_v_send_request( ip_addr_t *target_ip );
void arp_v_recv( arp_t *arp );

void arp_v_create_reply( arp_t *arp, 
						  eth_mac_addr_t *target_mac, 
						  ip_addr_t *sender_ip,
						  ip_addr_t *target_ip );
						  
void arp_v_create_request( arp_t *arp, ip_addr_t *target_ip );

#endif