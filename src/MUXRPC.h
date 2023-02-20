#ifndef MUXRPC_H
#define MUXRPC_H

#include <DataIO.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
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
  status_t writeToBuffer(unsigned char *buffer);
};

enum struct RequestType {
  MISSING,
  SOURCE,
  DUPLEX,
  ASYNC,
  UNKNOWN,
};

enum struct MethodMatch {
  NO_MATCH,
  WRONG_TYPE,
  MATCH,
};

class Method {
public:
  virtual MethodMatch check(unsigned char peer[crypto_sign_PUBLICKEYBYTES],
                            std::vector<BString> name, RequestType type);
  virtual status_t call(unsigned char peer[crypto_sign_PUBLICKEYBYTES],
                        RequestType type, BMessage *args,
                        BMessenger replyTo) = 0;

private:
  std::vector<BString> name;
  RequestType expectedType;
};

class Connection;

class Sender : public BHandler {
public:
  Sender(Connection *conn, int32 requestNumber);
  void MessageReceived(BMessage *msg);

private:
  Connection *conn;
  int32 requestNumber;
};

class Connection : public BLooper {
public:
private:
  status_t populateHeader(Header *out);
  status_t readOne();
  std::unique_ptr<BDataIO> inner;
  std::map<int32, BMessenger> inboundOngoing;
  std::map<int32, Sender> outboundOngoing;
  std::vector<Method *> *handlers;
  unsigned char peer[crypto_sign_PUBLICKEYBYTES];
  int32 nextRequest = 1;
};
}; // namespace muxrpc

#endif // MUXRPC_H
