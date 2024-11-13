#include "Tunnel.h"

namespace rooms2 {

Tunnel::Tunnel(muxrpc::Sender sender)
    :
    sender(sender),
    queueLock(create_sem(1, "Room tunnel queue lock")),
    trackEmpty(create_sem(0, "Room tunnel empty block")) {}

Tunnel::~Tunnel() {
  delete_sem(this->queueLock);
  delete_sem(this->trackEmpty);
}

ssize_t Tunnel::Read(void *buffer, size_t size) {
  return 0;
}

ssize_t Tunnel::Write(const void *buffer, size_t size) {
  return 0;
}

status_t Tunnel::push(void *buffer, size_t size) {
  return B_OK;
}
} // namespace rooms2
