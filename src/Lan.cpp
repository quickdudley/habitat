#include "Lan.h"
#include "Base64.h"
#include <NetworkInterface.h>
#include <NetworkRoster.h>

static BString
generatePayload(BNetworkAddress *addr, uint16 ssbPort,
                unsigned char pubkey[crypto_sign_PUBLICKEYBYTES]) {
  BString result("net:");
  result.Append(addr->ToString(false));
  result += ':';
  result << (int)ssbPort;
  result += "~shs:";
  result.Append(
      base64::encode(pubkey, crypto_sign_PUBLICKEYBYTES, base64::STANDARD));
  return result;
}

int broadcastLoop(void *vargs) {
  BroadcastLoopArguments *args = (BroadcastLoopArguments *)vargs;
  thread_id thisThread = find_thread(NULL);
  bigtime_t microseconds = system_time();
  while (true) {
    // Check whether or not it's time to quit
    if (has_data(thisThread)) {
      thread_id sender;
      if (receive_data(&sender, NULL, 0) == 'STOP') {
        break;
      }
    }
    snooze_until(microseconds, B_SYSTEM_TIMEBASE);
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
        BNetworkAddress broadcastAddress = address.Broadcast();
        BString payload =
            generatePayload(&myAddress, args->ssbPort, args->pubkey);
        broadcastAddress.SetPort(8008);
        args->socket.SendTo(broadcastAddress, payload.String(),
                            (size_t)payload.Length());
      }
    }
    bigtime_t finishTimestamp = system_time();
    microseconds += 1000000;
    // The uncommon event that broadcasting takes more than 1 second.
    if (finishTimestamp > microseconds) {
      microseconds = finishTimestamp;
    }
  }
  return 0;
}

thread_id runBroadcastThread(BroadcastLoopArguments *args) {
  thread_id thread =
      spawn_thread(broadcastLoop, "LAN advertisement", 0, (void *)args);
  if (thread >= B_OK) {
    resume_thread(thread);
  }
  return thread;
}
