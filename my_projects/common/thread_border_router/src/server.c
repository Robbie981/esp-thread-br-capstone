#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "server.h"


// static void coap_request_handler(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
// {
//     OT_UNUSED_VARIABLE(aContext);

//     // Get payload length
//     uint16_t payloadLength = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);
//     char buf[payloadLength + 1]; // +1 for null terminator
//     otMessageRead(aMessage, otMessageGetOffset(aMessage), buf, payloadLength);
//     buf[payloadLength] = '\0'; // Null-terminate the string

//     // Log the received message
//     otCliOutputFormat("Received CoAP message: %s\n", buf);

//     // Prepare a response message
//     otMessage *response = otCoapNewMessage(esp_openthread_get_instance(), NULL);
//     if (response == NULL)
//     {
//         otCliOutputFormat("Failed to allocate response message\n");
//         return;
//     }

//     otCoapMessageInitResponse(response, aMessage, OT_COAP_TYPE_ACKNOWLEDGMENT, OT_COAP_CODE_CONTENT);
//     otCoapMessageSetPayloadMarker(response);

//     const char *responsePayload = "ACK: Message received!";
//     otMessageAppend(response, responsePayload, strlen(responsePayload));

//     // Send response
//     otError error = otCoapSendResponse(esp_openthread_get_instance(), response, aMessageInfo);
//     if (error != OT_ERROR_NONE)`
//     {
//         otCliOutputFormat("Failed to send CoAP response: %d\n", error);
//         otMessageFree(response);
//     }
// }

static void startCoapServer(uint16_t port)
{
    otError error = OT_ERROR_NONE;
    otInstance* instance = esp_openthread_get_instance();
    static otCoapResource coapResource;

    // Initialize the CoAP resource
    coapResource.mHandler = NULL;
    coapResource.mUriPath = "test"; // URI path for the CoAP resource
    coapResource.mContext = NULL;
    coapResource.mNext = NULL;

    // Start CoAP server
    error = otCoapStart(instance, port);
    
    if (error != OT_ERROR_NONE) {
        otLogCritPlat("Failed to start COAP server.");
    } else {
        otLogNotePlat("Started CoAP server at port %d.", port);
    }

    // Add the resource to the CoAP server
    otCoapAddResource(instance, &coapResource);

    otLogNotePlat("CoAP server initialized. Listening on resource: %s\n", coapResource.mUriPath);
    return;
}

/**
 * Callback function to start UDP/CoAP server
 */
void serverStartCallback(otChangedFlags changed_flags, void* ctx)
{
    OT_UNUSED_VARIABLE(ctx);
    static otDeviceRole s_previous_role = OT_DEVICE_ROLE_DISABLED;
    otInstance* instance = esp_openthread_get_instance();
    if (!instance) { return; }

    otDeviceRole role = otThreadGetDeviceRole(instance);
    if (role == OT_DEVICE_ROLE_LEADER && s_previous_role != OT_DEVICE_ROLE_DISABLED)  // disalbed -> leader
    {
        otLogNotePlat("Robbie: Role went from disabled to leader, starting CoAP server...\n");
        startCoapServer(OT_DEFAULT_COAP_PORT);
    }
    s_previous_role = role;
}