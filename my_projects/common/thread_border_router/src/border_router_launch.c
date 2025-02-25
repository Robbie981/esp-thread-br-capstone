/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "border_router_launch.h"
#include "coap_server.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_cli.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_cli_extension.h"
#include "esp_rcp_update.h"
#include "esp_vfs_eventfd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "openthread/backbone_router_ftd.h"
#include "openthread/border_router.h"
#include "openthread/cli.h"
#include "openthread/dataset_ftd.h"
#include "openthread/error.h"
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/logging.h"
#include "openthread/platform/radio.h"
#include "openthread/tasklet.h"
#include "openthread/thread_ftd.h"
#include "esp_wifi.h"

#if CONFIG_OPENTHREAD_CLI_WIFI
#include "esp_ot_wifi_cmd.h"
#endif

#if CONFIG_OPENTHREAD_BR_AUTO_START
#include "esp_wifi.h"
#include "example_common_private.h"
#include "protocol_examples_common.h"
#endif

/*************DEFINES START*************/
#define TAG "esp_ot_br"
#define RCP_VERSION_MAX_SIZE 100

#define MY_THREAD_PANID 0x2222 // 16 bit
#define MY_THREAD_EXT_PANID {0x22, 0x22, 0x00, 0x00, 0xab, 0xce, 0x11, 0x22} // 64 bit
#define MY_THREAD_NETWORK_NAME "my_ot_network"
#define MY_INTERFACE_ID {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}
#define MY_MESH_LOCAL_PREFIX {0xfd, 0x00, 0x00, 0x00, 0xfb, 0x01, 0x00, 0x01}                                                  // 64 bits, first 8 bits always 0xfd
#define MY_THREAD_NETWORK_KEY {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff} // ot default key
/*************DEFINES END*************/

static esp_openthread_platform_config_t s_openthread_platform_config;

#if CONFIG_AUTO_UPDATE_RCP
static void update_rcp(void)
{
    // Deinit uart to transfer UART to the serial loader
    esp_openthread_rcp_deinit();
    if (esp_rcp_update() == ESP_OK) {
        esp_rcp_mark_image_verified(true);
    } else {
        esp_rcp_mark_image_verified(false);
    }
    esp_restart();
}

static void try_update_ot_rcp(const esp_openthread_platform_config_t *config)
{
    char internal_rcp_version[RCP_VERSION_MAX_SIZE];
    const char *running_rcp_version = otPlatRadioGetVersionString(esp_openthread_get_instance());

    if (esp_rcp_load_version_in_storage(internal_rcp_version, sizeof(internal_rcp_version)) == ESP_OK) {
        ESP_LOGI(TAG, "Internal RCP Version: %s", internal_rcp_version);
        ESP_LOGI(TAG, "Running  RCP Version: %s", running_rcp_version);
        if (strcmp(internal_rcp_version, running_rcp_version) == 0) {
            esp_rcp_mark_image_verified(true);
        } else {
            update_rcp();
        }
    } else {
        ESP_LOGI(TAG, "RCP firmware not found in storage, will reboot to try next image");
        esp_rcp_mark_image_verified(false);
        esp_restart();
    }
}
#endif // CONFIG_AUTO_UPDATE_RCP

static void rcp_failure_handler(void)
{
#if CONFIG_AUTO_UPDATE_RCP
    esp_rcp_mark_image_unusable();
    char internal_rcp_version[RCP_VERSION_MAX_SIZE];
    if (esp_rcp_load_version_in_storage(internal_rcp_version, sizeof(internal_rcp_version)) == ESP_OK) {
        ESP_LOGI(TAG, "Internal RCP Version: %s", internal_rcp_version);
        update_rcp();
    } else {
        ESP_LOGI(TAG, "RCP firmware not found in storage, will reboot to try next image");
        esp_rcp_mark_image_verified(false);
        esp_restart();
    }
#endif
}

static void ot_br_wifi_init(void *ctx)
{
#if CONFIG_OPENTHREAD_CLI_WIFI
    ESP_ERROR_CHECK(esp_ot_wifi_config_init());
#endif
#if CONFIG_OPENTHREAD_BR_AUTO_START
#if CONFIG_EXAMPLE_CONNECT_WIFI || CONFIG_EXAMPLE_CONNECT_ETHERNET
    bool wifi_or_ethernet_connected = false;
#else
#error No backbone netif!
#endif
#if CONFIG_EXAMPLE_CONNECT_WIFI
    char wifi_ssid[32] = "";
    char wifi_password[64] = "";
    if (esp_ot_wifi_config_get_ssid(wifi_ssid) == ESP_OK) {
        ESP_LOGI(TAG, "use the Wi-Fi config from NVS");
        esp_ot_wifi_config_get_password(wifi_password);
    } else {
        ESP_LOGI(TAG, "use the Wi-Fi config from Kconfig");
        strcpy(wifi_ssid, CONFIG_WIFI_SSID);
        strcpy(wifi_password, CONFIG_WIFI_PASSWORD);
    }
    if (esp_ot_wifi_connect(wifi_ssid, wifi_password) == ESP_OK) {
        wifi_or_ethernet_connected = true;
    } else {
        ESP_LOGE(TAG, "Fail to connect to Wi-Fi, please try again manually");
    }
#endif
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(example_ethernet_connect());
    wifi_or_ethernet_connected = true;
#endif
    if (wifi_or_ethernet_connected) {
        esp_openthread_lock_acquire(portMAX_DELAY);
        esp_openthread_set_backbone_netif(get_example_netif());
        ESP_ERROR_CHECK(esp_openthread_border_router_init());
#if CONFIG_EXAMPLE_CONNECT_WIFI
        esp_ot_wifi_border_router_init_flag_set(true);
#endif
        otOperationalDatasetTlvs dataset;
        otError error = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &dataset);
        ESP_ERROR_CHECK(esp_openthread_auto_start((error == OT_ERROR_NONE) ? &dataset : NULL));
        esp_openthread_lock_release();
    } else {
        ESP_LOGE(TAG, "Auto-start mode failed, please try to start manually");
    }
#endif // CONFIG_OPENTHREAD_BR_AUTO_START
    vTaskDelete(NULL);
}

/**@brief Function for adding an IPv6-address. */
void addIPv6Address(void)
{
    otInstance *myInstance = esp_openthread_get_instance();
    otNetifAddress aAddress;
    const otMeshLocalPrefix *ml_prefix = otThreadGetMeshLocalPrefix(myInstance);
    uint8_t interfaceID[8]= MY_INTERFACE_ID;
    
    memcpy(&aAddress.mAddress.mFields.m8[0], ml_prefix, 8);
    memcpy(&aAddress.mAddress.mFields.m8[8], interfaceID, 8);
    
    otError error = otIp6AddUnicastAddress(myInstance, &aAddress);
    
    if (error != OT_ERROR_NONE)
        ESP_LOGW("Robbie: ", "addIPAdress Error: %d\r\n", error);
}

static void config_thread_dataset(void)
{
    otInstance *myOtInstance = esp_openthread_get_instance();
    otOperationalDataset aDataset;

    // overwrite current active dataset
    ESP_ERROR_CHECK(otDatasetGetActive(myOtInstance, &aDataset));
    memset(&aDataset, 0, sizeof(otOperationalDataset));

    aDataset.mActiveTimestamp.mSeconds = 0;
    aDataset.mComponents.mIsActiveTimestampPresent = true;

    aDataset.mChannel = 11;
    aDataset.mComponents.mIsChannelPresent = true;

    aDataset.mChannelMask = (otChannelMask)0x7fff800; // enables all radio channels (11–26)
    aDataset.mComponents.mIsChannelMaskPresent = true;

    aDataset.mPanId = (otPanId)MY_THREAD_PANID;
    aDataset.mComponents.mIsPanIdPresent = true;

    uint8_t extPanId[OT_EXT_PAN_ID_SIZE] = MY_THREAD_EXT_PANID;
    memcpy(aDataset.mExtendedPanId.m8, extPanId, sizeof(aDataset.mExtendedPanId));
    aDataset.mComponents.mIsExtendedPanIdPresent = true;

    uint8_t key[OT_NETWORK_KEY_SIZE] = MY_THREAD_NETWORK_KEY;
    memcpy(aDataset.mNetworkKey.m8, key, sizeof(aDataset.mNetworkKey));
    aDataset.mComponents.mIsNetworkKeyPresent = true;

    static char aNetworkName[] = MY_THREAD_NETWORK_NAME;
    size_t length = strlen(aNetworkName);
    assert(length <= OT_NETWORK_NAME_MAX_SIZE);
    memcpy(aDataset.mNetworkName.m8, aNetworkName, length);
    aDataset.mComponents.mIsNetworkNamePresent = true;

    uint8_t meshLocalPrefix[OT_MESH_LOCAL_PREFIX_SIZE] = MY_MESH_LOCAL_PREFIX;
    memcpy(aDataset.mMeshLocalPrefix.m8, meshLocalPrefix, sizeof(aDataset.mMeshLocalPrefix));
    aDataset.mComponents.mIsMeshLocalPrefixPresent = true;

    otDatasetSetActive(myOtInstance, &aDataset); // cli command: dataset commit active
}

static void ot_task_worker(void *ctx)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *openthread_netif = esp_netif_new(&cfg);

    assert(openthread_netif != NULL);

    // Initialize the OpenThread stack
    esp_openthread_register_rcp_failure_handler(rcp_failure_handler);
    esp_openthread_set_compatibility_error_callback(rcp_failure_handler);
    ESP_ERROR_CHECK(esp_openthread_init(&s_openthread_platform_config));
#if CONFIG_AUTO_UPDATE_RCP
    try_update_ot_rcp(&s_openthread_platform_config);
#endif
    // Initialize border routing features
    esp_openthread_lock_acquire(portMAX_DELAY);
    ESP_ERROR_CHECK(esp_netif_attach(openthread_netif, esp_openthread_netif_glue_init(&s_openthread_platform_config)));
#if CONFIG_OPENTHREAD_LOG_LEVEL_DYNAMIC
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
#endif

    esp_openthread_cli_init();
    esp_cli_custom_command_init();
    esp_openthread_cli_create_task();
    esp_openthread_lock_release();

    xTaskCreate(ot_br_wifi_init, "ot_br_wifi_init", 6144, NULL, 4, NULL);

     /**
     * Set up the callback  to start transferring UDP/CoAP messages
     * the moment the border router attaches to the a Thread network.
     */
    otSetStateChangedCallback(esp_openthread_get_instance(), serverStartCallback, NULL);

    // Configure custome thread dataset
    config_thread_dataset();

    // Add an IPv6 address
    addIPv6Address();

    // Run the main loop
    esp_openthread_launch_mainloop(); //this loop blocks

    // Clean up
    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);
    esp_vfs_eventfd_unregister();
    esp_rcp_update_deinit();
    vTaskDelete(NULL);
}

void launch_openthread_border_router(const esp_openthread_platform_config_t *platform_config,
                                     const esp_rcp_update_config_t *update_config)
{
    s_openthread_platform_config = *platform_config;

#if CONFIG_AUTO_UPDATE_RCP
    ESP_ERROR_CHECK(esp_rcp_update_init(update_config));
#else
    OT_UNUSED_VARIABLE(update_config);
#endif

    xTaskCreate(ot_task_worker, "ot_br_main", 8192, xTaskGetCurrentTaskHandle(), 5, NULL);
}
