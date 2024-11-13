#include "Tunnel.h"
#include <algorithm>
#include <cstring>

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
  acquire_sem(this->trackEmpty);
  acquire_sem(this->queueLock);
  auto &chunk = this->queue.front();
  size = std::max(size, chunk.count - this->progress);
  std::memcpy(buffer, chunk.bytes.get() + this->progress, size);
  this->progress += size;
  if (this->progress >= chunk.count) {
  	this->progress = 0;
  	this->queue.pop();
  } else {
  	release_sem(this->trackEmpty);
  }
  release_sem(this->queueLock);
  return size;
}

ssize_t Tunnel::Write(const void *buffer, size_t size) {
  return 0;
}

status_t Tunnel::push(void *buffer, size_t size) {
  status_t err = acquire_sem(this->queueLock);
  if (err != B_NO_ERROR)
    return err;
  std::unique_ptr<char[]> bytes(new char[size]);
  this->queue.push({std::move(bytes), size});
  release_sem(this->trackEmpty);
  release_sem(this->queueLock);
  return B_OK;
}
} // namespace rooms2
