#ifndef LISTENER_H
#define LISTENER_H

#include "MUXRPC.h"
#include "Secret.h"
#include <Messenger.h>
#include <Socket.h>
#include <memory>
#include <sodium.h>

class SSBListener {
public:
  SSBListener(std::shared_ptr<Ed25519Secret> myId, BMessenger broadcaster);
  virtual thread_id run();
  virtual void halt();

private:
  static int trampoline(void *);
  int run_();
  std::shared_ptr<Ed25519Secret> myId;
  thread_id task = -1;
  std::unique_ptr<BAbstractSocket> listenSocket;
  BMessenger broadcaster;
};

void registerMethod(std::shared_ptr<muxrpc::Method> method);

#endif // LISTENER_H
