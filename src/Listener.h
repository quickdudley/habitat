#ifndef LISTENER_H
#define LISTENER_H

#include <Messenger.h>
#include <Socket.h>
#include <memory>
#include <sodium.h>

class SSBListener {
public:
  SSBListener(unsigned char pubkey[crypto_sign_PUBLICKEYBYTES],
              BMessenger broadcaster);
  virtual thread_id run();
  virtual void halt();

private:
  static int trampoline(void *);
  int run_();
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
  thread_id task = -1;
  std::unique_ptr<BAbstractSocket> listenSocket;
  BMessenger broadcaster;
};

#endif // LISTENER_H
