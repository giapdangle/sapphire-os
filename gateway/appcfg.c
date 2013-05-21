
#include "cpu.h"

#include "config.h"
#include "keyvalue.h"


#include "appcfg.h"

KV_SECTION_META kv_meta_t app_cfg_kv[] = {
    { KV_GROUP_SYS_CFG, CFG_PARAM_ETH_MAC_ADDRESS,              SAPPHIRE_TYPE_MAC48,        0,  0, cfg_i8_kv_handler,  "ethernet_mac" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_SNTP_SERVER,                  SAPPHIRE_TYPE_STRING128,    0,  0, cfg_i8_kv_handler,  "sntp_server" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_SNTP_SYNC_INTERVAL,           SAPPHIRE_TYPE_UINT16,       0,  0, cfg_i8_kv_handler,  "sntp_sync_interval" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_ENABLE_SNTP,                  SAPPHIRE_TYPE_BOOL,         0,  0, cfg_i8_kv_handler,  "enable_sntp" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_ENABLE_DHCP,                  SAPPHIRE_TYPE_BOOL,         0,  0, cfg_i8_kv_handler,  "enable_dhcp" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_BROADCAST_PORT_0,             SAPPHIRE_TYPE_UINT16,       0,  0, cfg_i8_kv_handler,  "broadcast_port_0" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_BROADCAST_PORT_0_LOCAL,       SAPPHIRE_TYPE_BOOL,         0,  0, cfg_i8_kv_handler,  "broadcast_port_0_local" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_BROADCAST_PORT_1,             SAPPHIRE_TYPE_UINT16,       0,  0, cfg_i8_kv_handler,  "broadcast_port_1" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_BROADCAST_PORT_1_LOCAL,       SAPPHIRE_TYPE_BOOL,         0,  0, cfg_i8_kv_handler,  "broadcast_port_1_local" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_BROADCAST_PORT_2,             SAPPHIRE_TYPE_UINT16,       0,  0, cfg_i8_kv_handler,  "broadcast_port_2" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_BROADCAST_PORT_2_LOCAL,       SAPPHIRE_TYPE_BOOL,         0,  0, cfg_i8_kv_handler,  "broadcast_port_2_local" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_BROADCAST_PORT_3,             SAPPHIRE_TYPE_UINT16,       0,  0, cfg_i8_kv_handler,  "broadcast_port_3" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_BROADCAST_PORT_3_LOCAL,       SAPPHIRE_TYPE_BOOL,         0,  0, cfg_i8_kv_handler,  "broadcast_port_3_local" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_ENABLE_TIME_SOURCE,           SAPPHIRE_TYPE_BOOL,         0,  0, cfg_i8_kv_handler,  "enable_time_source" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_NETWORK_CHANNEL,              SAPPHIRE_TYPE_UINT8,        0,  0, cfg_i8_kv_handler,  "network_channel" },
};

