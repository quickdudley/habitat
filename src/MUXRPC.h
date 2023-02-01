#ifndef MUXRPC_H
#define MUXRPC_H

#include <DataIO.h>
#include <memory>

namespace muxrpc {
class Connection {
  Connection(std::unique_ptr<BDataIO> inner, unsigned char peer[32]);
};

struct header {
  unsigned char flags;
  uint32 bodyLength;
  uint32 requestNumber;
};
}; // namespace muxrpc

#endif // MUXRPC_H
