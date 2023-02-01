#include "Listener.h"
#include <Handler.h>

SSBListener::SSBListener(BAbstractSocket *srv, BMessenger sink)
    :
    listenSocket(srv),
    sink(sink) {}

int SSBListener::run_() {
  class Cleanup : public BHandler {
  public:
    Cleanup(BAbstractSocket *skt)
        :
        skt(skt) {}
    void MessageReceived(BMessage *msg) {
      if (msg->what == B_NO_REPLY) {
        delete this->skt;
      }
      delete this;
    }

  private:
    BAbstractSocket *skt;
  };
  thread_id thisThread = find_thread(NULL);
  while (true) {
    if (has_data(thisThread)) {
      thread_id sender;
      if (receive_data(&sender, NULL, 0) == 'STOP') {
        break;
      }
    }
    BAbstractSocket *peer;
    int err = this->listenSocket->Accept(peer);
    if (err < B_OK)
      return err;
    BMessage msg('NCON');
    Cleanup *cleanup = new Cleanup(peer);
    msg.AddPointer("socket", (BDataIO *)peer);
    msg.AddPointer("listener", this);
    this->sink.SendMessage(&msg, cleanup);
  }
  return 0;
}

thread_id SSBListener::run() {
  this->task = spawn_thread(trampoline, "SSB Listener", 0, this);
  if (this->task < B_OK)
    return task;
  status_t err = resume_thread(task);
  if (err < B_OK)
    return err;
  return this->task;
}

void SSBListener::halt() {
  int skt = this->listenSocket->Socket();
  if (skt > 0) {
  	shutdown(skt, SHUT_RDWR);
  }
  if (this->task > 0) {
  	send_data(this->task, 'STOP', NULL, 0);
  }
}

int SSBListener::trampoline(void *data) {
  return ((SSBListener *)data)->run_();
}