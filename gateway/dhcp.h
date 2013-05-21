
#ifndef _DHCP_H
#define _DHCP_H

#include "ip.h"
#include "sockets.h"
#include "config.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_STATUS_UNCONFIGURED 0
#define DHCP_STATUS_CONFIGURED 1

typedef struct{
	uint8_t op; // opcode
	uint8_t htype; // hardware address type
	uint8_t hlen;  // hardware address length
	uint8_t hops;
	uint32_t xid; // transaction id
	uint16_t secs; // elapsed time
	uint16_t flags; // bootp flags
	ip_addr_t ciaddr; // client IP address
	ip_addr_t yiaddr; // your IP address
	ip_addr_t siaddr; // next server IP address
	ip_addr_t giaddr; // relay agent IP address
	uint8_t chaddr[16]; // client hardware address
	char sname[64];
	char file[128];
	uint32_t magic_cookie;
} dhcp_t;

#define DHCP_PACKET_BUF_SIZE 512

// op
#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY 2

// htype
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HTYPE_CHAOS 5

// hlen
#define DHCP_HLEN_ETHERNET 6
#define DHCP_HLEN_IEEE_802_15_4 8

// magic cookie
#define DHCP_MAGIC_COOKIE 0x63538263

// option types
#define DHCP_OPTION_PAD 0

#define DHCP_OPTION_SUBNET_MASK 1

#define DHCP_OPTION_ROUTER 3

#define DHCP_OPTION_DNS_SERVER 6

#define DHCP_OPTION_HOST_NAME 12

#define DHCP_OPTION_REQUESTED_IP 50

#define DHCP_OPTION_IP_LEASE_TIME 51

#define DHCP_OPTION_OVERLOAD 52
	#define DHCP_OVERLOAD_FILE 1
	#define DHCP_OVERLOAD_SNAME 2
	#define DHCP_OVERLOAD_BOTH 3

#define DHCP_OPTION_MESSAGE_TYPE 53
	#define DHCP_MESSAGE_TYPE_DHCPDISCOVER 	1
	#define DHCP_MESSAGE_TYPE_DHCPOFFER 	2
	#define DHCP_MESSAGE_TYPE_DHCPREQUEST 	3
	#define DHCP_MESSAGE_TYPE_DHCPDECLINE 	4
	#define DHCP_MESSAGE_TYPE_DHCPACK 		5
	#define DHCP_MESSAGE_TYPE_DHCPNAK 		6
	#define DHCP_MESSAGE_TYPE_DHCPRELEASE 	7
		
#define DHCP_OPTION_SERVER_IDENTIFIER 54

#define DHCP_OPTION_PARAMETER_REQUEST_LIST 55

#define DHCP_OPTION_VENDOR_CLASS_ID 60

#define DHCP_OPTION_CLIENT_ID 61

#define DHCP_OPTION_FQDN 81 // client fully qualified domain name

#define DHCP_OPTION_END 255

// this struct holds all information from options which are relevant to
// this implementation
typedef struct{
	uint8_t type;           // option 53
	ip_addr_t subnet_mask;  // option 1
    ip_addr_t dns_server;   // option 6
    ip_addr_t router;       // option 3
	ip_addr_t server_ip;    // option 54
	uint32_t ip_lease_time; // option 51
	uint8_t overload;       // option 52
	ip_addr_t your_addr;
} dhcp_data_t;

// this structure holds the current dhcp configuration
typedef struct{
	ip_addr_t ip_addr;
	ip_addr_t subnet_mask;
	ip_addr_t server_ip;
	ip_addr_t router_ip;
	ip_addr_t dns_server_ip;
	uint32_t ip_lease_time;
} dhcp_config_t;

// thread state
typedef struct{
    uint32_t timer;
    uint32_t timeout;
    socket_t sock;
    sock_addr_t raddr;
    ip_addr_t requested_ip;
    uint8_t client_id[64];
    uint8_t client_id_len;
    char hostname[CFG_STR_LEN];
    uint8_t pkt_buf[DHCP_PACKET_BUF_SIZE];
    uint16_t pkt_len;
    uint32_t xid;
    dhcp_data_t dhcp_data;
    uint8_t status;
} dhcp_thread_state_t;

typedef thread_t dhcp_thread_t;

dhcp_thread_t dhcp_t_create_client( uint8_t *client_id, 
                                    uint8_t client_id_len, 
                                    char *host_name,
                                    ip_addr_t requested_ip );

dhcp_thread_t dhcp_t_kill( dhcp_thread_t t );
uint8_t dhcp_u8_get_status( dhcp_thread_t t );
void dhcp_v_get_config( dhcp_thread_t t, dhcp_config_t *config );


#endif

