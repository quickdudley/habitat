#include "Listener.h"
#include <Application.h>
#include <Handler.h>

SSBListener::SSBListener(unsigned char pubkey[crypto_sign_PUBLICKEYBYTES],
                         BMessenger broadcaster)
    :
    broadcaster(broadcaster) {
  memcpy(this->pubkey, pubkey, crypto_sign_PUBLICKEYBYTES);
}

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
    BAbstractSocket *peer;
    // TODO: this->listenSocket->Accept(peer) etc
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