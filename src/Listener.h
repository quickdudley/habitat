#ifndef LISTENER_H
#define LISTENER_H

#include "Blob.h"
#include "MUXRPC.h"
#include "Secret.h"
#include <Messenger.h>
#include <Socket.h>
#include <memory>
#include <sodium.h>

class SSBListener {
public:
  SSBListener(std::shared_ptr<Ed25519Secret> myId, BMessenger broadcaster,
              const muxrpc::MethodSuite &methods);
  virtual thread_id run();
  virtual void halt();

private:
  static int trampoline(void *);
  int run_();
  std::shared_ptr<Ed25519Secret> myId;
  muxrpc::MethodSuite methods;
  thread_id task = -1;
  std::unique_ptr<BAbstractSocket> listenSocket;
  BMessenger broadcaster;
};

#endif // LISTENER_H
