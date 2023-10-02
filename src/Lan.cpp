#include "Lan.h"
#include "Base64.h"
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <iostream>

static BString
generatePayload(BNetworkAddress *addr,
                unsigned char pubkey[crypto_sign_PUBLICKEYBYTES]) {
  BString result("net:");
  result += addr->ToString(false);
  result += ':';
  result << addr->Port();
  result += "~shs:";
  result.Append(
      base64::encode(pubkey, crypto_sign_PUBLICKEYBYTES, base64::STANDARD));
  return result;
}

LanBroadcaster::LanBroadcaster(unsigned char pubkey[crypto_sign_PUBLICKEYBYTES])
    :
    socket(std::make_unique<BDatagramSocket>()) {
  {
    BNetworkAddress local;
    local.SetToWildcard(AF_INET, 8008);
    this->socket->Bind(local, true);
    this->socket->SetBroadcast(true);
  }
  memcpy(this->pubkey, pubkey, crypto_sign_PUBLICKEYBYTES);
}

void LanBroadcaster::MessageReceived(BMessage *message) {
  status_t error = B_ERROR;
  BMessage reply;
  switch (message->what) {
  case 'BEGN': {
    uint16 port;
    if ((error = message->FindUInt16("port", &port)) != B_OK)
      break;
    this->ssbPort = port;
    BMessage tick('TICK');
    this->timer =
        std::make_unique<BMessageRunner>(BMessenger(this), tick, 999999);
  } break;
  case 'STOP':
    this->timer->SetCount(0);
    this->timer.reset(NULL);
    error = B_OK;
    break;
  case 'TICK':
    error = this->sendBroadcast();
    break;
  default:
    BHandler::MessageReceived(message);
  }
  reply.AddInt32("error", error);
  if (error != B_OK)
    reply.AddString("message", strerror(error));
  message->SendReply(&reply);
}

status_t LanBroadcaster::sendBroadcast() {
  // Check which network interfaces exist
  BNetworkRoster &roster = BNetworkRoster::Default();
  BNetworkInterface interface;
  uint32 rosterCookie = 0;
  while (roster.GetNextInterface(&rosterCookie, interface) == B_OK) {
    // Check which addresses exist for this interface
    int32 count = interface.CountAddresses();
    for (int32 i = 0; i < count; i++) {
      BNetworkInterfaceAddress address;
      if (interface.GetAddressAt(i, address) != B_OK)
        break;
      BNetworkAddress myAddress = address.Address();
      myAddress.SetPort(this->ssbPort);
      if (!myAddress.IsEmpty() &&
          myAddress != BNetworkAddress("::1", this->ssbPort)) {
        BNetworkAddress broadcastAddress = address.Broadcast();
        BString payload = generatePayload(&myAddress, this->pubkey);
        broadcastAddress.SetPort(8008);
        this->socket->SendTo(broadcastAddress, payload.String(),
                             (size_t)payload.Length());
      }
    }
  }
  return B_OK;
}

std::shared_ptr<BDatagramSocket> LanBroadcaster::getSocket() {
  return this->socket;
}