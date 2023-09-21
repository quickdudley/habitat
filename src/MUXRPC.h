#ifndef MUXRPC_H
#define MUXRPC_H

#include <DataIO.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <map>
#include <memory>
#include <queue>
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
  status_t readFromBuffer(unsigned char *buffer);
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
                            std::vector<BString> &name, RequestType type);
  virtual status_t call(unsigned char peer[crypto_sign_PUBLICKEYBYTES],
                        RequestType type, BMessage *args, BMessenger replyTo,
                        BMessenger *inbound) = 0;

protected:
  std::vector<BString> name;
  RequestType expectedType;
};

class Connection;

// For putting messages into a priority queue
class MessageOrder {
public:
  bool operator()(BMessage &a, BMessage &b);
};

class Sender {
public:
  Sender(BMessenger inner);
  ~Sender();
  status_t send(BMessage *content, bool stream, bool error,
                bool inOrder = true);
  status_t send(BString &content, bool stream, bool error, bool inOrder = true);
  status_t send(unsigned char *content, uint32 length, bool stream, bool error,
                bool inOrder = true);

private:
  BMessenger inner;
  uint32 sequence;
  sem_id sequenceSemaphore;
};

class SenderHandler : public BHandler {
public:
  ~SenderHandler();
  void MessageReceived(BMessage *msg) override;

private:
  SenderHandler(Connection *conn, int32 requestNumber);
  BDataIO *output();
  void actuallySend(const BMessage *wrapper);
  std::priority_queue<BMessage, std::vector<BMessage>, MessageOrder> outOfOrder;
  int32 requestNumber;
  uint32 sentSequence = 0;
  friend class Connection;
};

struct Inbound {
  BMessenger target;
  uint32 sequence;
};

class Connection : public BLooper {
public:
  Connection(std::unique_ptr<BDataIO> inner);
  ~Connection();
  thread_id Run() override;
  void Quit() override;
  status_t request(std::vector<BString> &name, RequestType type, BMessage *args,
                   BMessenger replyTo, BMessenger *outbound);

private:
  status_t populateHeader(Header *out);
  status_t readOne();
  int32 pullLoop();
  SenderHandler *findSend(uint32 requestNumber);
  thread_id pullThreadID = B_NO_MORE_THREADS;
  std::unique_ptr<BDataIO> inner;
  std::map<int32, Inbound> inboundOngoing;
  sem_id ongoingLock;
  std::shared_ptr<std::vector<std::shared_ptr<Method>>> handlers;
  unsigned char peer[crypto_sign_PUBLICKEYBYTES];
  int32 nextRequest = 1;
  friend BDataIO *SenderHandler::output();
  static int32 pullThreadFunction(void *data);
};
}; // namespace muxrpc

#endif // MUXRPC_H
