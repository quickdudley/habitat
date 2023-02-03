#ifndef MUXRPC_H
#define MUXRPC_H

#include <DataIO.h>
#include <memory>
#include <sodium.h>

namespace muxrpc {

struct header {
  unsigned char flags;
  uint32 bodyLength;
  uint32 requestNumber;
};

class Connection {
public:
  Connection(std::unique_ptr<BDataIO> inner, unsigned char peer[32]);
  status_t populateHeader(header *out);

private:
  std::unique_ptr<BDataIO> inner;
  unsigned char peer[crypto_sign_PUBLICKEYBYTES];
};
}; // namespace muxrpc

#endif // MUXRPC_H
