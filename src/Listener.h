#ifndef LISTENER_H
#define LISTENER_H

#include "Blob.h"
#include "MUXRPC.h"
#include "Secret.h"
#include <Messenger.h>
#include <Socket.h>
#include <memory>
#include <sodium.h>

class DefaultCall {
public:
  virtual void call(muxrpc::Connection *rpc) = 0;
};

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
void registerDefaultCall(std::shared_ptr<DefaultCall> call);

#endif // LISTENER_H
