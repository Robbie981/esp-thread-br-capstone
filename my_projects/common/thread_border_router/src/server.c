#include "server.h"


static void startCoapServer(uint16_t port)
{
  otError error = otCoapStart(OT_INSTANCE, port);

  if (error != OT_ERROR_NONE) {
    otLogCritPlat("Failed to start COAP server.");
  } else {
    otLogNotePlat("Started CoAP server at port %d.", port);
  }
  return;
}

static void startUdpServer(otDeviceRole role)
{
  EmptyMemory(&udpSocket, sizeof(otUdpSocket));

  otUdpReceive handler = NULL;
#if EXPERIMENT_THROUGHPUT_UDP
  handler = tpUdpRequestHandler;
#elif EXPERIMENT_PACKET_LOSS_UDP
  otLogNotePlat("Creating the server for the Packet Loss UDP experiment.");
  handler = plUdpRequestHandler;
#endif
  assert(handler != NULL);

  handleError(otUdpOpen(OT_INSTANCE, &udpSocket, handler, NULL),
              "Failed to open UDP socket.");

  udpSockAddr.mAddress = *otThreadGetMeshLocalEid(OT_INSTANCE);
  udpSockAddr.mPort = UDP_SOCK_PORT;
  handleError(otUdpBind(OT_INSTANCE, &udpSocket, &udpSockAddr, OT_NETIF_THREAD),
              "Failed to set up UDP server.");
  
  otLogNotePlat("Created UDP server at port %d.", UDP_SOCK_PORT);
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

  if ((connected(role) == true) && (connected(s_previous_role) == false))
  {
    printNetworkKey();

    otError error = otThreadBecomeLeader(OT_INSTANCE);
    if (error == OT_ERROR_NONE)
    {
      otLogNotePlat("Successfully attached to the Thread Network as the leader.");
    }
    else
    {
      otLogCritPlat("Failed to become the Leader of the Thread Network.");
      otLogCritPlat("Going to restart.");

      esp_restart();
    }

    expStartUdpServer(role);
  }
  s_previous_role = role;
  return;
}