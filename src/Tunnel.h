#ifndef TUNNEL_H
#define TUNNEL_H

#include "MUXRPC.h"
#include <DataIO.h>
#include <Handler.h>
#include <queue>

namespace rooms2 {
namespace __priv__ {
struct Chunk {
  std::unique_ptr<char[]> bytes;
  size_t count;
};
} // namespace __priv__

class Tunnel : public BDataIO {
public:
  Tunnel(BMessenger sender);
  ~Tunnel();
  ssize_t Read(void *buffer, size_t size) override;
  ssize_t Write(const void *buffer, size_t size) override;
  status_t push(void *buffer, size_t size);

private:
  muxrpc::Sender sender;
  sem_id queueLock;
  sem_id trackEmpty;
  std::queue<__priv__::Chunk> queue;
  size_t progress = 0;
};

class TunnelReader : public BHandler {
public:
  TunnelReader(Tunnel *sink);
  void MessageReceived(BMessage *message);
  ~TunnelReader();

private:
  Tunnel *sink;
};
} // namespace rooms2

#endif
