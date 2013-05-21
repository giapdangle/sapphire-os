
#include "system.h"
#include "cpu.h"

#include "keyvalue.h"

#include "notifications.h"

static uint16_t kv_attached_device;

#define KV_GROUP_GATEWAY_NOTIFY		KV_GROUP_APP_BASE

#define KV_ID_DEVICE_ATTACH 		1

KV_SECTION_META kv_meta_t gateway_kv[] = {
    { KV_GROUP_GATEWAY_NOTIFY, KV_ID_DEVICE_ATTACH,       SAPPHIRE_TYPE_UINT16,   KV_FLAGS_READ_ONLY, &kv_attached_device, 0,  "device_attach" },
};

void notif_v_device_attach( uint16_t short_addr ){

	kv_attached_device = short_addr;

	kv_i8_notify( KV_GROUP_GATEWAY_NOTIFY, KV_ID_DEVICE_ATTACH );
}



