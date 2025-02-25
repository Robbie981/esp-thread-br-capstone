#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "coap_server.h"
#include "misc.h"
#include "esp_log.h"
#include "esp_http_client.h"

static void coap_request_handler(void * p_context, otMessage * p_message, const otMessageInfo * p_message_info);
static void coap_response_send(otMessage * p_request_message, const otMessageInfo * p_message_info);

//Define CoAP-ressource
static otCoapResource m_coap_resource = { .mUriPath = "coapdata",
                                        .mHandler = coap_request_handler,
                                        .mContext = NULL,
                                        .mNext    = NULL
                                        };

//Define buffer for received message text
char msg_rcvd[30];
uint16_t msg_rcvd_length = 0;

/**@brief Function for handling a CoAP-request. 
*  Just store the recieved message and print the message.
*  If confirmable message, response with an acknowledgement. */
static void coap_request_handler(void * p_context, otMessage * p_message, const otMessageInfo * p_message_info)
{
    OT_UNUSED_VARIABLE(p_context);
    otCoapCode messageCode = otCoapMessageGetCode(p_message);
    otCoapType messageType = otCoapMessageGetType(p_message);
    
    do 
    {
        if ((messageType != OT_COAP_TYPE_CONFIRMABLE && messageType != OT_COAP_TYPE_NON_CONFIRMABLE) || messageCode != OT_COAP_CODE_PUT) 
            break;

        // Parse message and print to cli
        msg_rcvd_length = otMessageRead(p_message, otMessageGetOffset(p_message), msg_rcvd, 29);
        msg_rcvd[msg_rcvd_length]='\0';
        otCliOutputFormat(msg_rcvd);
        otCliOutputFormat("\r\n");
    
        if (messageType == OT_COAP_TYPE_CONFIRMABLE) // If confirmable message, reply with confirmation
        {
            ESP_LOGI(LOCAL_DEBUG_TAG, "Sending CoAP confirmation...");
            coap_response_send(p_message, p_message_info);
        } 
            
    } while (false);
}

/**@brief Function for sending a response for a request. */
static void coap_response_send(otMessage * p_request_message, const otMessageInfo * p_message_info)
{
    otError error = OT_ERROR_NO_BUFS;
    otMessage  *p_response;
    
    //Create new message
    p_response = otCoapNewMessage(OT_INSTANCE, NULL);
    if (p_response == NULL) 
    {
        ESP_LOGW(LOCAL_DEBUG_TAG, "Failed to allocate message for CoAP Request\r\n");
        return;
    }
    
    do 
    {
        //Add CoAP type and code to the message  
        error = otCoapMessageInitResponse(p_response, p_request_message,
                                        OT_COAP_TYPE_ACKNOWLEDGMENT, 
                                        OT_COAP_CODE_CHANGED);
        if (error != OT_ERROR_NONE) { break; }
    
        //Send the response
        error = otCoapSendResponse(OT_INSTANCE, p_response, p_message_info);
    } while (false);
    
    if (error != OT_ERROR_NONE) 
    {
        ESP_LOGW(LOCAL_DEBUG_TAG, "Failed to send store data response: %d\r\n", error);
        otMessageFree(p_response);
    }
}

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

esp_err_t client_event_post_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        printf("HTTP_EVENT_ON_DATA: %.*s\n", evt->data_len, (char *)evt->data);
        break;

    default:
        break;
    }
    return ESP_OK;
}

static void post_rest_function()
{
    esp_http_client_config_t config_post = {
        .url = "http://httpbin.org/post",
        .method = HTTP_METHOD_POST,
        .cert_pem = NULL,
        .event_handler = client_event_post_handler};
        
    esp_http_client_handle_t client = esp_http_client_init(&config_post);

    char  *post_data = "test ...";
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
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
        // ESP_LOGI(LOCAL_DEBUG_TAG, "Role changed to leader, starting CoAP server...");
        // startCoapServer(OT_DEFAULT_COAP_PORT);
        ESP_LOGI(LOCAL_DEBUG_TAG, "Role changed to leader, testing HTTP...");
        post_rest_function();
    }
    s_previous_role = role;
}