#include "Tunnel.h"
#include <algorithm>
#include <cstring>

namespace rooms2 {

Tunnel::Tunnel(BMessenger sender)
    :
    sender(sender),
    queueLock(create_sem(1, "Room tunnel queue lock")),
    trackEmpty(create_sem(0, "Room tunnel empty block")) {}

Tunnel::~Tunnel() {
  delete_sem(this->queueLock);
  delete_sem(this->trackEmpty);
  this->sender.send(false, true, true, true);
}

ssize_t Tunnel::Read(void *buffer, size_t size) {
  release_sem(this->trackEmpty); // hack to prevent deadlocks
  while (true) {
    acquire_sem(this->queueLock);
    if (this->queue.empty()) {
      release_sem(this->queueLock);
      if (status_t err;
          (err = acquire_sem_etc(this->trackEmpty, 1, B_RELATIVE_TIMEOUT,
                                 300000000)) != B_OK) {
        return err;
      }
    } else {
      break;
    }
  }
  auto &chunk = this->queue.front();
  if (chunk.count == 0) {
    release_sem(this->queueLock);
    return B_BUSTED_PIPE;
  }
  size = std::min(size, chunk.count - this->progress);
  std::memcpy(buffer, chunk.bytes.get() + this->progress, size);
  this->progress += size;
  if (this->progress >= chunk.count) {
    this->progress = 0;
    this->queue.pop();
  }
  release_sem(this->queueLock);
  return size;
}

ssize_t Tunnel::Write(const void *buffer, size_t size) {
  size = std::min(size, (size_t)65535);
  status_t err;
  if ((err = this->sender.send((unsigned char *)buffer, (uint32)size, true,
                               false, false)) == B_OK) {
    return size;
  } else {
    return err;
  }
}

status_t Tunnel::push(void *buffer, size_t size, bool locked) {
  if (!locked) {
    status_t err = acquire_sem(this->queueLock);
    if (err != B_NO_ERROR)
      return err;
  }
  std::unique_ptr<char[]> bytes(size > 0 ? new char[size] : NULL);
  if (size > 0)
    std::memcpy(bytes.get(), buffer, size);
  this->queue.push({std::move(bytes), size});
  release_sem(this->trackEmpty);
  if (!locked)
    release_sem(this->queueLock);
  return B_OK;
}

sem_id Tunnel::getLock() { return this->queueLock; }

TunnelReader::TunnelReader(Tunnel *sink)
    :
    sink(sink),
    queueLock(sink->getLock()) {}

TunnelReader::~TunnelReader() {
  if (acquire_sem(this->queueLock) == B_OK) {
    this->sink->push(NULL, 0, true);
    release_sem(this->queueLock);
  }
}

void TunnelReader::MessageReceived(BMessage *message) {
  unsigned char *data;
  ssize_t bytes;
  if (message->FindData("content", B_RAW_TYPE, (const void **)&data, &bytes) !=
      B_OK) {
    goto cleanup;
  }
  if (bytes == 0)
    goto cleanup;
  if (acquire_sem(this->queueLock) == B_OK) {
    this->sink->push(data, bytes, true);
    release_sem(this->queueLock);
  } else {
    this->Looper()->RemoveHandler(this);
    delete this;
  }
cleanup:
  if (message->GetBool("end", false) || !message->GetBool("stream", true)) {
    this->Looper()->RemoveHandler(this);
    delete this;
  }
}
} // namespace rooms2
