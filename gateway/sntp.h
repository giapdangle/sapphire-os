

#ifndef _SNTP_H
#define _SNTP_H

#include "wcom_time.h"

#define SNTP_SERVER_PORT    123

// minimum poll interval in seconds
// changing (reducing) this violates the RFC
#define SNTP_MINIMUM_POLL_INTERVAL      15

#define SNTP_DEFAULT_POLL_INTERVAL      60

#define SNTP_TIMEOUT                    10000
#define SNTP_TRIES                      4

// NTP Packet
typedef struct{
    uint8_t li_vn_mode;
    uint8_t stratum;
    uint8_t poll;
    int8_t precision;
    int32_t root_delay;
    uint32_t root_dispersion;
    uint32_t reference_identifier;
    ntp_ts_t reference_timestamp;
    ntp_ts_t originate_timestamp;
    ntp_ts_t receive_timestamp;
    ntp_ts_t transmit_timestamp;
    // the key identifer and digest are not implemented
} ntp_packet_t;

#define SNTP_LI_MASK            0b11000000
#define SNTP_LI_NO_WARNING      0b00000000
#define SNTP_LI_61_SECONDS      0b01000000
#define SNTP_LI_59_SECONDS      0b10000000
#define SNTP_LI_ALARM           0b11000000

#define SNTP_VN_MASK            0b00111000
#define SNTP_VERSION_4          0b00100000

#define SNTP_MODE_MASK          0b00000111
#define SNTP_MODE_CLIENT        0b00000011
#define SNTP_MODE_SERVER        0b00000100
#define SNTP_MODE_BROADCAST     0b00000101

#define SNTP_STRATUM_KOD        0 // kiss of death
#define SNTP_STRATUM_PRIMARY    1
#define SNTP_STRATUM_SECONDARY  2 // to 15
#define SNTP_STRATUM_RESERVED   16 // to 255

typedef int8_t sntp_status_t8;
#define SNTP_STATUS_NO_SYNC             0
#define SNTP_STATUS_SYNCHRONIZED        1
#define SNTP_STATUS_DISABLED            -1


void sntp_v_init( void );

sntp_status_t8 sntp_u8_get_status( void );

ntp_ts_t sntp_t_now( void );
ntp_ts_t sntp_t_last_sync( void );

ntp_ts_t sntp_ts_from_ms( uint32_t ms );
uint16_t sntp_u16_get_fraction_as_ms( ntp_ts_t t );

int16_t sntp_i16_get_offset( void );
uint16_t sntp_u16_get_delay( void );

#endif

