#ifndef LAN_H
#define LAN_H

#include <DatagramSocket.h>
#include <sodium.h>

struct BroadcastLoopArguments {
  uint16 ssbPort;
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
  BDatagramSocket socket; // Assumed to already be in broadcast mode
};

int broacastLoop(BroadcastLoopArguments *vargs);
thread_id runBroadcastThread(BroadcastLoopArguments *args);

#endif // LAN_H
