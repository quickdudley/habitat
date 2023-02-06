#ifndef MUXRPC_H
#define MUXRPC_H

#include <DataIO.h>
#include <memory>
#include <sodium.h>

namespace muxrpc {
enum struct BodyType {
  BINARY,
  UTF8_SRING,
  JSON,
};

struct Header {
  uint32 bodyLength;
  int32 requestNumber;
  unsigned char flags;
  BodyType bodyType();
  bool endOrError();
  bool stream();
  void setBodyType(BodyType value);
  void setEndOrError(bool value);
  void setStream(bool value);
};

class Connection {
public:
  Connection(std::unique_ptr<BDataIO> inner, unsigned char peer[32]);
  status_t populateHeader(Header *out);

private:
  std::unique_ptr<BDataIO> inner;
  unsigned char peer[crypto_sign_PUBLICKEYBYTES];
};
}; // namespace muxrpc

#endif // MUXRPC_H
