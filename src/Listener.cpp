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

namespace {
class PrintReply : public BHandler {
  void MessageReceived(BMessage *msg) override;
};

void PrintReply::MessageReceived(BMessage *msg) {
  msg->PrintToStream();
  BHandler::MessageReceived(msg);
}

std::shared_ptr<std::vector<std::shared_ptr<muxrpc::Method>>> defaultHandlers =
    std::make_shared<std::vector<std::shared_ptr<muxrpc::Method>>>(
        std::vector<std::shared_ptr<muxrpc::Method>>());
} // namespace

int SSBListener::run_() {
  BHandler *printer = new PrintReply;
  be_app->Lock();
  be_app->AddHandler(printer);
  be_app->Unlock();
  thread_id thisThread = find_thread(NULL);
  this->listenSocket = std::make_unique<BSocket>();
  {
    BNetworkAddress local;
    local.SetToWildcard(AF_INET, 0);
    this->listenSocket->Bind(local, true);
    this->listenSocket->Listen();
    this->listenSocket->SetTimeout(5000000);
    local = this->listenSocket->Local();
    std::cerr << "Listening on port " << local.Port() << std::endl;
    BMessage message('BEGN');
    message.AddUInt16("port", local.Port());
    this->broadcaster.SendMessage(&message);
  }
  while (true) {
    if (has_data(thisThread)) {
      thread_id sender;
      if (receive_data(&sender, NULL, 0) == 'STOP') {
        break; // TODO: prevent leaked thread ID
      }
    }
    BAbstractSocket *peer;
    if (this->listenSocket->Accept(peer) == B_OK) {
      peer->SetTimeout(B_INFINITE_TIMEOUT);
      std::unique_ptr<BoxStream> shsPeer;
      try {
        shsPeer = std::make_unique<BoxStream>(std::unique_ptr<BDataIO>(peer),
                                              SSB_NETWORK_ID, myId.get());
      } catch (HandshakeError err) {
        continue;
      } catch (...) {
        delete peer;
        throw;
      }
      muxrpc::Connection *rpc =
          new muxrpc::Connection(std::move(shsPeer), defaultHandlers);
      be_app->RegisterLooper(rpc);
      thread_id thread = rpc->Run();
      std::vector<BString> call = {"blobs", "get"};
      BMessage args('JSAR');
      // rpc->request(call, muxrpc::RequestType::ASYNC, &args,
      // BMessenger(printer), NULL);

      // For initial testing/debugging just do one connection at a time
      status_t exitValue;
      wait_for_thread(thread, &exitValue);
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
    resume_thread(this->task);
    wait_for_thread(this->task, &exitValue);
  }
}

int SSBListener::trampoline(void *data) {
  return ((SSBListener *)data)->run_();
}