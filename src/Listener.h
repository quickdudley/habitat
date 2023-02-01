#ifndef LISTENER_H
#define LISTENER_H

#include <Messenger.h>
#include <Socket.h>
#include <memory>

class SSBListener {
public:
  SSBListener(BAbstractSocket *, BMessenger sink);
  virtual thread_id run();
  virtual void halt();

private:
  static int trampoline(void *);
  int run_();
  thread_id task = -1;
  std::unique_ptr<BAbstractSocket> listenSocket;
  BMessenger sink;
};

#endif // LISTENER_H
