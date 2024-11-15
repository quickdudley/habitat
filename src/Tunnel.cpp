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
}

ssize_t Tunnel::Read(void *buffer, size_t size) {
  while (true) {
    acquire_sem(this->queueLock);
    if (this->queue.empty()) {
      release_sem(this->queueLock);
      acquire_sem(this->trackEmpty);
    } else {
      break;
    }
  }
  auto &chunk = this->queue.front();
  size = std::max(size, chunk.count - this->progress);
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
  size = std::min(size, (size_t)65536);
  status_t err;
  if ((err = this->sender.send((unsigned char *)buffer, (uint32)size, true,
                               false, false)) == B_OK) {
    return size;
  } else {
    return err;
  }
}

status_t Tunnel::push(void *buffer, size_t size) {
  status_t err = acquire_sem(this->queueLock);
  if (err != B_NO_ERROR)
    return err;
  std::unique_ptr<char[]> bytes(new char[size]);
  std::memcpy(bytes.get(), buffer, size);
  this->queue.push({std::move(bytes), size});
  release_sem(this->trackEmpty);
  release_sem(this->queueLock);
  return B_OK;
}

TunnelReader::TunnelReader(Tunnel *sink)
    :
    sink(sink) {}

TunnelReader::~TunnelReader() { this->sink->push(NULL, 0); }

void TunnelReader::MessageReceived(BMessage *message) {
  unsigned char *data;
  ssize_t bytes;
  if (message->FindData("content", B_RAW_TYPE, (const void **)&data, &bytes) !=
      B_OK) {
    goto cleanup;
  }
  this->sink->push(data, bytes);
cleanup:
  if (message->GetBool("end", false) || !message->GetBool("stream", true)) {
    this->Looper()->RemoveHandler(this);
    delete this;
  }
}
} // namespace rooms2
