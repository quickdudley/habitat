#ifndef MUXRPC_H
#define MUXRPC_H

#include <DataIO.h>
#include <String.h>
#include <map>
#include <memory>
#include <sodium.h>
#include <vector>

namespace muxrpc {
enum struct BodyType {
  BINARY,
  UTF8_STRING,
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

class Endpoint {};

class Stream {};

class Connection {
public:
  Connection(
      std::unique_ptr<BDataIO> inner,
      std::map<std::vector<BString>, std::unique_ptr<Endpoint>> *endpoints,
      unsigned char peer[32]);

private:
  status_t populateHeader(Header *out);
  status_t readOne();
  std::map<std::vector<BString>, std::unique_ptr<Endpoint>> *endpoints;
  std::map<int32, std::unique_ptr<Stream>> ongoing;
  std::unique_ptr<BDataIO> inner;
  unsigned char peer[crypto_sign_PUBLICKEYBYTES];
  int32 nextRequest = 1;
};
}; // namespace muxrpc

#endif // MUXRPC_H
