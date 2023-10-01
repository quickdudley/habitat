#include "Listener.h"
#include "Connection.h"
#include "MUXRPC.h"
#include <Application.h>
#include <Handler.h>
#include <iostream>

SSBListener::SSBListener(std::shared_ptr<Ed25519Secret> myId,
                         BMessenger broadcaster)
    :
    myId(myId),
    broadcaster(broadcaster) {}

int SSBListener::run_() {
  thread_id thisThread = find_thread(NULL);
  this->listenSocket = std::make_unique<BSocket>();
  {
    BNetworkAddress local;
    local.SetToWildcard(AF_INET, 0);
    this->listenSocket->Bind(local, true);
    local = this->listenSocket->Local();
    BMessage message('BEGN');
    message.AddUInt16("port", 8008);
    this->broadcaster.SendMessage(&message);
  }
  while (true) {
    if (has_data(thisThread)) {
      thread_id sender;
      if (receive_data(&sender, NULL, 0) == 'STOP') {
        break; // TODO: prevent leaked thread ID
      }
    }
    try {
      BAbstractSocket *peer;
      std::unique_ptr<BoxStream> shsPeer;
      this->listenSocket->Accept(peer);
      try {
        shsPeer = std::make_unique<BoxStream>(std::unique_ptr<BDataIO>(peer),
                                              SSB_NETWORK_ID, myId.get());
      } catch (...) {
        delete peer;
        throw;
      }
      muxrpc::Connection *rpc = new muxrpc::Connection(std::move(shsPeer));
      be_app->RegisterLooper(rpc);
      rpc->Run();
      std::cerr << "MUXRPC connection opened!" << std::endl;
    } catch (HandshakeError err) {
      // TODO Print this
    }
  }
  return 0;
}

thread_id SSBListener::run() {
  this->task = spawn_thread(trampoline, "SSB Listener", 0, this);
  if (this->task < B_OK)
    return this->task;
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
    status_t exitValue;
    wait_for_thread(this->task, &exitValue);
  }
}

int SSBListener::trampoline(void *data) {
  return ((SSBListener *)data)->run_();
}