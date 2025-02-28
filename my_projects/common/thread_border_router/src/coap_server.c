#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "coap_server.h"
#include "misc.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"

// static esp_err_t client_event_post_handler(esp_http_client_event_t *evt)
// {
//     switch (evt->event_id)
//     {
//     case HTTP_EVENT_ON_DATA:
//         if (evt->data_len > 0) {
//             printf("HTTP_EVENT_ON_DATA: %.*s\n", evt->data_len, (char *)evt->data);
//         }
//         break;
//     default:
//         break;
//     }
//     return ESP_OK;
// }

static void post_to_database(char *data)
{
    esp_http_client_config_t config_post = {
        .url = "http://httpbin.org/post",
        .method = HTTP_METHOD_POST,
        .event_handler = NULL,
        .keep_alive_enable = true
    };
        
    esp_http_client_handle_t client = esp_http_client_init(&config_post);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, data, strlen(data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(LOCAL_DEBUG_TAG, "HTTP POST Status = %d, content_length = %" PRId64,
            esp_http_client_get_status_code(client),
            esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(LOCAL_DEBUG_TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

/**@brief Function for handling a CoAP-request. */
static void coap_request_handler(void * p_context, otMessage * p_message, const otMessageInfo * p_message_info)
{
    OT_UNUSED_VARIABLE(p_context);
    
    otCoapCode messageCode = otCoapMessageGetCode(p_message);
    otCoapType messageType = otCoapMessageGetType(p_message);

    char payload_rcvd[256];
    int payload_len;

    switch (messageType)
    {
        case OT_COAP_TYPE_NON_CONFIRMABLE:
        {
            switch (messageCode)
            {
                case OT_COAP_CODE_PUT:
                    // Read the message payload
                    payload_len = otMessageRead(p_message, otMessageGetOffset(p_message), payload_rcvd, sizeof(payload_rcvd) - 1);
                    
                    if (payload_len < 0)
                    {
                        ESP_LOGE(LOCAL_DEBUG_TAG, "Failed to read CoAP message.");
                        break;
                    }

                    payload_rcvd[payload_len] = '\0'; // NULL terminate
                    otCliOutputFormat("Received CoAP Payload:\n%s\n", payload_rcvd);
                    // Send the payload to the database via HTTP POST
                    post_to_database(payload_rcvd);
                    break;

                default:
                    ESP_LOGW(LOCAL_DEBUG_TAG, "Unhandled CoAP Code: %d", messageCode);
                    break;
            }
            break;
        }

        case OT_COAP_TYPE_CONFIRMABLE:
            ESP_LOGI(LOCAL_DEBUG_TAG, "Wrong CoAP Message Type, ignoring.");
            break;

        case OT_COAP_TYPE_ACKNOWLEDGMENT:
            ESP_LOGI(LOCAL_DEBUG_TAG, "Wrong CoAP Message Type, ignoring.");
            break;

        case OT_COAP_TYPE_RESET:
            ESP_LOGI(LOCAL_DEBUG_TAG, "Wrong CoAP Message Type, ignoring.");
            break;

    }
}

//Define CoAP-ressource
static otCoapResource m_coap_resource = { .mUriPath = "coapdata",
    .mHandler = coap_request_handler,
    .mContext = NULL,
    .mNext    = NULL
    };

/**
 * Starts a CoAP server and adds CoAP resources
 */
static void startCoapServer(uint16_t port)
{
    otError error = OT_ERROR_NONE;

    do{
        // Try to start CoAP server
        error = otCoapStart(OT_INSTANCE, port);
        if (error != OT_ERROR_NONE) 
        {
            ESP_LOGW(LOCAL_DEBUG_TAG, "Failed to start COAP server.");
            break; 
        }
        // Add the resource to the CoAP server
        otCoapAddResource(OT_INSTANCE, &m_coap_resource);
        ESP_LOGI(LOCAL_DEBUG_TAG, "Started CoAP server at port %d.", port);

    } while(false);

    return;
}

/**
 * Callback function to start UDP/CoAP server
 */
void serverStartCallback(otChangedFlags changed_flags, void* ctx)
{
    OT_UNUSED_VARIABLE(ctx);
    static otDeviceRole s_previous_role = OT_DEVICE_ROLE_DISABLED;

    if (!OT_INSTANCE) { return; }
    otDeviceRole role = otThreadGetDeviceRole(OT_INSTANCE);
    if (role == OT_DEVICE_ROLE_LEADER && s_previous_role != role)  // some role -> leader   
    {
        ESP_LOGI(LOCAL_DEBUG_TAG, "Role changed to leader, starting CoAP server...");
        startCoapServer(OT_DEFAULT_COAP_PORT);
    }
    s_previous_role = role;
}