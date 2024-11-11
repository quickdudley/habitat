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
  BString describe();
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

class Connection;

class Method {
public:
  virtual MethodMatch check(Connection *connection, std::vector<BString> &name,
                            RequestType type);
  virtual status_t call(Connection *connection, RequestType type,
                        BMessage *args, BMessenger replyTo,
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
  status_t send(bool content, bool stream, bool error, bool inOrder = true);
  status_t send(double content, bool stream, bool error, bool inOrder = true);
  status_t send(BMessage *content, bool stream, bool error,
                bool inOrder = true);
  status_t send(BString &content, bool stream, bool error, bool inOrder = true);
  status_t send(unsigned char *content, uint32 length, bool stream, bool error,
                bool inOrder = true);
  BMessenger *outbound();

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
  bool canceled = false;
  friend class Connection;
};

struct Inbound {
  BMessenger target;
  uint32 sequence;
};

class MethodSuite;

class Connection : public BLooper {
public:
  Connection(std::unique_ptr<BDataIO> inner, const MethodSuite &methods,
             const BString &serverName = "");
  ~Connection();
  thread_id Run() override;
  void Quit() override;
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void MessageReceived(BMessage *message) override;
  status_t request(const std::vector<BString> &name, RequestType type,
                   BMessage *args, BMessenger replyTo, BMessenger *outbound);
  BString cypherkey();

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
  std::map<BString, BMessenger> crossTalk;
  BString serverName;
  bool stoppedRecv = false;
  friend BDataIO *SenderHandler::output();
  friend void SenderHandler::actuallySend(const BMessage *wrapper);
  static int32 pullThreadFunction(void *data);
};

class ConnectionHook {
public:
  virtual void call(muxrpc::Connection *rpc) = 0;
};

class MethodSuite {
public:
  MethodSuite();
  MethodSuite(const MethodSuite &original, bool includeHooks = true);
  MethodSuite &operator=(const MethodSuite &original);
  void registerMethod(std::shared_ptr<Method> method);
  void registerConnectionHook(std::shared_ptr<ConnectionHook> call);
  void copyHooks(const MethodSuite &other);

private:
  std::shared_ptr<std::vector<std::shared_ptr<Method>>> methods;
  std::shared_ptr<std::vector<std::shared_ptr<ConnectionHook>>> connectionHooks;
  friend class Connection;
};
}; // namespace muxrpc

#endif // MUXRPC_H
