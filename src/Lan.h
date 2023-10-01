#ifndef LAN_H
#define LAN_H

#include <DatagramSocket.h>
#include <Handler.h>
#include <MessageRunner.h>
#include <memory>
#include <sodium.h>

class LanBroadcaster : public BHandler {
public:
  LanBroadcaster(unsigned char pubkey[crypto_sign_PUBLICKEYBYTES]);
  void MessageReceived(BMessage *message) override;

private:
  status_t sendBroadcast();
  uint16 ssbPort;
  std::shared_ptr<BDatagramSocket> socket;
  std::unique_ptr<BMessageRunner> timer;
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
};

#endif // LAN_H
