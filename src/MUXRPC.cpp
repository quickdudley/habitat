#include "MUXRPC.h"
#include "BJSON.h"
#include "Base64.h"
#include "Connection.h"
#include "JSON.h"
#include "Logging.h"
#include <Application.h>
#include <MessageRunner.h>
#include <PropertyInfo.h>
#include <support/ByteOrder.h>
#include <utility>

namespace muxrpc {

MethodMatch Method::check(Connection *conn, std::vector<BString> &name,
                          RequestType type) {
  if (name == this->name) {
    if (type == this->expectedType)
      return MethodMatch::MATCH;
    else
      return MethodMatch::WRONG_TYPE;
  } else {
    return MethodMatch::NO_MATCH;
  }
}

Sender::Sender(BMessenger inner)
    :
    inner(inner),
    sequenceSemaphore(create_sem(1, "MUXRPC packet ordering")),
    sequence(1) {}

SenderHandler::SenderHandler(Connection *conn, int32 requestNumber)
    :
    requestNumber(requestNumber) {
  if (conn->Lock()) {
    conn->AddHandler(this);
    conn->Unlock();
  }
}

Sender::~Sender() { delete_sem(this->sequenceSemaphore); }

SenderHandler::~SenderHandler() {}

#define SEND_FUNCTION(type, msgMethod)                                         \
  status_t Sender::send(type content, bool stream, bool error, bool inOrder,   \
                        BMessenger whenDone) {                                 \
    BMessage wrapper('SEND');                                                  \
    wrapper.msgMethod("content", content);                                     \
    wrapper.AddBool("stream", stream);                                         \
    wrapper.AddBool("end", error);                                             \
    if (inOrder) {                                                             \
      status_t result;                                                         \
      if ((result = acquire_sem(this->sequenceSemaphore)) < B_NO_ERROR)        \
        return result;                                                         \
      uint32 sequence = this->sequence++;                                      \
      release_sem(this->sequenceSemaphore);                                    \
      wrapper.AddUInt32("sequence", sequence);                                 \
    }                                                                          \
    return this->inner.SendMessage(&wrapper, whenDone);                        \
  }                                                                            \
  status_t Sender::sendBlocking(type content, bool stream, bool error,         \
                                bool inOrder) {                                \
    BMessage wrapper('SEND');                                                  \
    wrapper.msgMethod("content", content);                                     \
    wrapper.AddBool("stream", stream);                                         \
    wrapper.AddBool("end", error);                                             \
    BMessage reply;                                                            \
    if (inOrder) {                                                             \
      status_t result;                                                         \
      if ((result = acquire_sem(this->sequenceSemaphore)) < B_NO_ERROR)        \
        return result;                                                         \
      uint32 sequence = this->sequence++;                                      \
      release_sem(this->sequenceSemaphore);                                    \
      wrapper.AddUInt32("sequence", sequence);                                 \
    }                                                                          \
    return this->inner.SendMessage(&wrapper, &reply);                          \
  }

SEND_FUNCTION(bool, AddBool)

SEND_FUNCTION(double, AddDouble)

SEND_FUNCTION(BMessage *, AddMessage)

SEND_FUNCTION(BString &, AddString)
#undef SEND_FUNCTION

status_t Sender::send(unsigned char *content, uint32 length, bool stream,
                      bool error, bool inOrder, BMessenger whenDone) {
  BMessage wrapper('SEND');
  wrapper.AddData("content", B_RAW_TYPE, content, length, false);
  wrapper.AddBool("stream", stream);
  wrapper.AddBool("end", error);
  if (inOrder) {
    status_t result;
    if ((result = acquire_sem(this->sequenceSemaphore)) < B_NO_ERROR)
      return result;
    uint32 sequence = this->sequence++;
    release_sem(this->sequenceSemaphore);
    wrapper.AddUInt32("sequence", sequence);
  }
  return this->inner.SendMessage(&wrapper, whenDone);
}

status_t Sender::sendBlocking(unsigned char *content, uint32 length,
                              bool stream, bool error, bool inOrder) {
  BMessage wrapper('SEND');
  wrapper.AddData("content", B_RAW_TYPE, content, length, false);
  wrapper.AddBool("stream", stream);
  wrapper.AddBool("end", error);
  if (inOrder) {
    status_t result;
    if ((result = acquire_sem(this->sequenceSemaphore)) < B_NO_ERROR)
      return result;
    uint32 sequence = this->sequence++;
    release_sem(this->sequenceSemaphore);
    wrapper.AddUInt32("sequence", sequence);
  }
  BMessage reply;
  return this->inner.SendMessage(&wrapper, &reply);
}

BMessenger *Sender::outbound() { return &this->inner; }

void SenderHandler::MessageReceived(BMessage *msg) {
  bool finished = false;
  switch (msg->what) {
  case 'SEND': {
    uint32 sequence;
    if (msg->FindUInt32("sequence", &sequence) == B_OK) {
      uint32 nextSequence = this->sentSequence + 1;
      if (sequence <= nextSequence) {
        this->sentSequence = sequence;
        nextSequence = sequence + 1;
        this->actuallySend(msg);
        finished = finished || msg->GetBool("end", false);
        while (!this->outOfOrder.empty() &&
               (sequence = this->outOfOrder.top()->GetUInt32("sequence", 0)) <=
                   nextSequence) {
          this->sentSequence = sequence;
          nextSequence = sequence + 1;
          this->actuallySend(this->outOfOrder.top());
          finished = finished || this->outOfOrder.top()->GetBool("end", false);
          delete this->outOfOrder.top();
          this->outOfOrder.pop();
        }
      } else {
        this->outOfOrder.push(this->Looper()->DetachCurrentMessage());
      }
    } else {
      this->actuallySend(msg);
      finished = finished || msg->GetBool("end", false);
    }
    break;
  }
  default:
    BHandler::MessageReceived(msg);
  }
  if (this->canceled || finished || !msg->GetBool("stream", true)) {
    auto conn = this->Looper();
    conn->Lock();
    conn->RemoveHandler(this);
    conn->Unlock();
    delete this;
  }
}

void SenderHandler::actuallySend(BMessage *wrapper) {
  Header header;
  if (this->canceled) {
    header.setBodyType(BodyType::JSON);
    header.bodyLength = 4;
    header.requestNumber = this->requestNumber;
    header.setEndOrError(true);
    header.setStream(true);
    unsigned char packet[13];
    header.writeToBuffer(packet);
    memcpy(packet + 9, "true", 4);
    this->output()->WriteExactly(packet, 13);
    return;
  }
  header.setEndOrError(wrapper->GetBool("end", true));
  header.setStream(wrapper->GetBool("stream", true));
  header.requestNumber = this->requestNumber;
  {
    const void *data;
    ssize_t length;
    if (wrapper->FindData("content", B_RAW_TYPE, &data, &length) == B_OK) {
      header.setBodyType(BodyType::BINARY);
      header.bodyLength = length;
      unsigned char headerBytes[9];
      header.writeToBuffer(headerBytes);
      BDataIO *output = this->output();
      output->WriteExactly(headerBytes, 9);
      output->WriteExactly(data, length);
      return;
    }
  }
  {
    BString content;
    class ExtractContent : public JSON::SerializerStart {
    public:
      ExtractContent(BString *target, Header *header)
          :
          JSON::SerializerStart(target, 0, false),
          target(target),
          header(header) {}
      void addNumber(const BString &rawname, const BString &name,
                     const BString &raw, JSON::number value) override {
        if (name == "content") {
          BString blank;
          this->header->setBodyType(BodyType::JSON);
          JSON::SerializerStart::addNumber(blank, blank, raw, value);
        }
      }
      void addBool(const BString &rawname, const BString &name,
                   bool value) override {
        if (name == "content") {
          BString blank;
          this->header->setBodyType(BodyType::JSON);
          JSON::SerializerStart::addBool(blank, blank, value);
        }
      }
      void addNull(const BString &rawname, const BString &name) override {
        if (name == "content") {
          BString blank;
          this->header->setBodyType(BodyType::JSON);
          JSON::SerializerStart::addNull(blank, blank);
        }
      }
      void addString(const BString &rawname, const BString &name,
                     const BString &raw, const BString &value) override {
        if (name == "content") {
          this->header->setBodyType(BodyType::UTF8_STRING);
          *this->target = value;
        }
      }
      std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                          const BString &name) override {
        BString blank;
        if (name == "content") {
          this->header->setBodyType(BodyType::JSON);
          return JSON::SerializerStart::addObject(blank, blank);
        } else {
          return JSON::NodeSink::addObject(blank, blank);
        }
      }
      std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                         const BString &name) override {
        BString blank;
        if (name == "content") {
          this->header->setBodyType(BodyType::JSON);
          return JSON::SerializerStart::addObject(blank, blank);
        } else {
          return JSON::NodeSink::addObject(blank, blank);
        }
      }

    private:
      BString *target;
      Header *header;
    };
    {
      JSON::RootSink rootsink(
          std::make_unique<ExtractContent>(&content, &header));
      JSON::fromBMessageObject(&rootsink, wrapper);
    }
    header.bodyLength = content.Length();
    BDataIO *output = this->output();
    unsigned char headerBytes[9];
    header.writeToBuffer(headerBytes);
    if (output->WriteExactly(headerBytes, 9) != B_OK) {
      BLooper *looper = this->Looper();
      if (looper->Lock())
        looper->Quit();
    }
    if (output->WriteExactly(content.String(), content.Length()) != B_OK) {
      BLooper *looper = this->Looper();
      if (looper->Lock())
        looper->Quit();
    } else {
      if (wrapper->ReturnAddress().IsValid())
        wrapper->SendReply('SENT');
    }
    return;
  }
}

namespace {
class DummyOutput : public BDataIO {
public:
  ssize_t Read(void *buffer, size_t size) override;
  ssize_t Write(const void *buffer, size_t size) override;
};

ssize_t DummyOutput::Read(void *buffer, size_t size) { return B_ERROR; }

ssize_t DummyOutput::Write(const void *buffer, size_t size) { return B_ERROR; }

DummyOutput dummyOutput;
} // namespace

BDataIO *SenderHandler::output() {
  auto conn = dynamic_cast<Connection *>(this->Looper());
  if (conn->stoppedRecv)
    return &dummyOutput;
  else
    return conn->inner.get();
}

namespace {
class Setup : public BHandler {
public:
  Setup(std::shared_ptr<std::vector<std::shared_ptr<ConnectionHook>>>
            connectionHooks);
  void MessageReceived(BMessage *message);

private:
  std::shared_ptr<std::vector<std::shared_ptr<ConnectionHook>>> connectionHooks;
};

Setup::Setup(std::shared_ptr<std::vector<std::shared_ptr<ConnectionHook>>>
                 connectionHooks)
    :
    connectionHooks(connectionHooks) {}

void Setup::MessageReceived(BMessage *message) {
  auto connection = dynamic_cast<Connection *>(this->Looper());
  for (auto hook : *this->connectionHooks)
    hook->call(connection);
  connection->Lock();
  connection->RemoveHandler(this);
  connection->Unlock();
  delete this;
}
}; // namespace

Connection::Connection(std::unique_ptr<BDataIO> inner,
                       const MethodSuite &methods, const BString &serverName)
    :
    BLooper("MUXRPC sender"),
    handlers(methods.methods),
    serverName(serverName) {
  this->inner = std::move(inner);
  this->ongoingLock = create_sem(1, "MUXRPC incoming streams lock");
  {
    auto setup = new Setup(methods.connectionHooks);
    this->AddHandler(setup);
    BMessenger(setup).SendMessage('RUN_');
  }
  BoxStream *shs = dynamic_cast<BoxStream *>(this->inner.get());
  if (shs)
    shs->getPeerKey(this->peer);
  else
    randombytes_buf(this->peer, crypto_sign_PUBLICKEYBYTES);
}

Connection::~Connection() {
  for (int32 i = 0; i < this->CountHandlers();) {
    SenderHandler *handler = dynamic_cast<SenderHandler *>(this->HandlerAt(i));
    if (handler)
      delete handler;
    else
      i++;
  }
  acquire_sem(this->ongoingLock);
  for (auto &link : this->inboundOngoing) {
    BMessage stop('MXRP');
    stop.AddBool("content", false);
    stop.AddBool("end", true);
    link.second.target.SendMessage(&stop);
  }
  delete_sem(this->ongoingLock);
  be_app->Lock();
  be_app->UnregisterLooper(this);
  be_app->Unlock();
}

thread_id Connection::Run() {
  this->pullThreadID =
      spawn_thread(Connection::pullThreadFunction, "MUXRPC receiver", 10, this);
  resume_thread(this->pullThreadID);
  return BLooper::Run();
}

void Connection::Quit() {
  if (this->pullThreadID != B_NO_MORE_THREADS) {
    send_data(this->pullThreadID, 'STOP', NULL, 0);
    status_t exitValue;
    int32 locks = this->CountLocks();
    for (int32 i = 0; i < locks; i++)
      this->Unlock();
    // The loop is in case the other thread blocks immediately after we unblock
    // it.
    while (this->Lock()) {
      if (this->stoppedRecv) {
        this->Unlock();
        break;
      }
      this->Unlock();
      suspend_thread(this->pullThreadID);
      resume_thread(this->pullThreadID);
    }
    wait_for_thread(this->pullThreadID, &exitValue);
    for (int32 i = 0; i < locks; i++)
      this->Lock();
  }
  if (this->Lock()) {
    for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
      if (BHandler *handler = this->HandlerAt(i); handler && handler != this)
        delete handler;
    }
    this->Unlock();
  }
  if (acquire_sem(this->ongoingLock) == B_OK)
    release_sem(this->ongoingLock);
  for (auto &hook : this->cleanup)
    hook();
  BLooper::Quit();
}

enum { kCrossTalk, kCreateCrossTalk };

static property_info connectionProperties[] = {
    {"CrossTalk",
     {B_GET_PROPERTY, B_DELETE_PROPERTY, 0},
     {B_NAME_SPECIFIER, 0},
     "A messenger set up by a MUXRPC handler for coordination with other "
     "handlers",
     kCrossTalk,
     {B_MESSENGER_TYPE}},
    {"CrossTalk",
     {B_CREATE_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "A messenger set up by a MUXRPC handler for coordination with other "
     "handlers",
     kCreateCrossTalk,
     {B_MESSENGER_TYPE}},
    {0}};

status_t Connection::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat+muxrpc-connection");
  BPropertyInfo propertyInfo(connectionProperties);
  data->AddFlat("messages", &propertyInfo);
  return BLooper::GetSupportedSuites(data);
}

BHandler *Connection::ResolveSpecifier(BMessage *msg, int32 index,
                                       BMessage *specifier, int32 what,
                                       const char *property) {
  BPropertyInfo propertyInfo(connectionProperties);
  if (propertyInfo.FindMatch(msg, index, specifier, what, property) >= 0)
    return this;
  return BHandler::ResolveSpecifier(msg, index, specifier, what, property);
}

void Connection::MessageReceived(BMessage *message) {
  if (!message->HasSpecifiers())
    return BLooper::MessageReceived(message);
  BMessage reply(B_REPLY);
  status_t error = B_ERROR;
  int32 index;
  BMessage specifier;
  int32 what;
  const char *property;
  uint32 match;
  if (message->GetCurrentSpecifier(&index, &specifier, &what, &property) !=
      B_OK) {
    return BLooper::MessageReceived(message);
  }
  BPropertyInfo propertyInfo(connectionProperties);
  propertyInfo.FindMatch(message, index, &specifier, what, property, &match);
  switch (match) {
  case kCrossTalk: {
    BString name;
    if (specifier.FindString("name", &name) != B_OK) {
      error = B_BAD_DATA;
      break;
    }
    switch (message->what) {
    case B_DELETE_PROPERTY: {
      error = B_OK;
      this->crossTalk.erase(name);
    } break;
    case B_GET_PROPERTY: {
      if (auto messenger = this->crossTalk.find(name);
          messenger != this->crossTalk.end()) {
        error = B_OK;
        reply.AddMessenger("result", messenger->second);
      } else {
        error = B_NAME_NOT_FOUND;
      }
    } break;
    }
  } break;
  case kCreateCrossTalk: {
    BString name;
    BMessenger messenger;
    if (message->FindString("name", &name) != B_OK) {
      error = B_BAD_DATA;
      break;
    }
    if (message->FindMessenger("messenger", &messenger) != B_OK) {
      error = B_BAD_DATA;
      break;
    }
    this->crossTalk.insert_or_assign(name, messenger);
    error = B_OK;
  } break;
  }
  reply.AddInt32("error", error);
  if (error != B_OK)
    reply.AddString("message", strerror(error));
  if (message->ReturnAddress().IsValid())
    message->SendReply(&reply);
}

status_t Connection::populateHeader(Header *out) {
  unsigned char buffer[9];
  status_t last_error;
  if ((last_error = this->inner->ReadExactly(buffer, 9)) != B_OK)
    return last_error;
  return out->readFromBuffer(buffer);
}

status_t Connection::request(const std::vector<BString> &name, RequestType type,
                             BMessage *args, BMessenger replyTo,
                             BMessenger *outbound) {
  SenderHandler *handler;
  try {
    int32 requestNumber = this->nextRequest++;
    // TODO: This will probably never actually reach INT32_MAX, but
    // we should ensure request numbers still in use are skipped if it does
    if (this->nextRequest == INT32_MAX)
      this->nextRequest = 1;
    handler = new SenderHandler(this, requestNumber);
    BMessage content('JSOB');
    {
      BMessage methodName('JSAR');
      for (uint32 i = 0; i < name.size(); i++) {
        BString k;
        k << i;
        methodName.AddString(k.String(), name[i]);
      }
      content.AddMessage("name", &methodName);
    }
    switch (type) {
    case RequestType::SOURCE:
      content.AddString("type", "source");
      break;
    case RequestType::DUPLEX:
      content.AddString("type", "duplex");
      break;
    case RequestType::ASYNC:
      content.AddString("type", "async");
      break;
    default:
      break;
    }
    content.AddMessage("args", args);
    if (type == RequestType::DUPLEX && outbound)
      *outbound = BMessenger(handler);
    if (acquire_sem(this->ongoingLock) == B_OK) {
      this->inboundOngoing.insert({-requestNumber, {replyTo, 1}});
      release_sem(this->ongoingLock);
    }
    return Sender(BMessenger(handler))
        .send(&content, type != RequestType::ASYNC, false, false);
  } catch (...) {
    delete handler;
    throw;
  }
}

int32 Connection::pullLoop() {
  status_t result;
  do {
    result = readOne();
    if (has_data(this->pullThreadID)) {
      thread_id sender;
      if (receive_data(&sender, NULL, 0) == 'STOP')
        result = B_CANCELED;
    }
  } while (result == B_OK);
  this->Lock();
  this->stoppedRecv = true;
  this->Unlock();
  {
    BString logtext("Closing connection to ");
    logtext << this->cypherkey() << ": " << strerror(result);
    writeLog('MXRP', logtext);
  }
  BMessenger(this).SendMessage(B_QUIT_REQUESTED);
  return result;
}

int32 Connection::pullThreadFunction(void *data) {
  return ((Connection *)data)->pullLoop();
}

SenderHandler *Connection::findSend(uint32 requestNumber) {
  this->Lock();
  for (int32 i = 0; i < this->CountHandlers(); i++) {
    SenderHandler *handler = dynamic_cast<SenderHandler *>(this->HandlerAt(i));
    if (handler != NULL && handler->requestNumber == requestNumber)
      return handler;
  }
  return NULL;
}

status_t Header::readFromBuffer(unsigned char *buffer) {
  status_t last_error;
  this->flags = buffer[0];
  if ((this->flags & 3) == 3)
    return B_BAD_DATA;
  memcpy(&(this->bodyLength), buffer + 1, sizeof(uint32));
  if ((last_error = swap_data(B_UINT32_TYPE, &this->bodyLength, sizeof(uint32),
                              B_SWAP_BENDIAN_TO_HOST)) != B_OK) {
    return last_error;
  }
  memcpy(&(this->requestNumber), buffer + 5, sizeof(uint32));
  if ((last_error = swap_data(B_INT32_TYPE, &this->requestNumber,
                              sizeof(uint32), B_SWAP_BENDIAN_TO_HOST)) !=
      B_OK) {
    return last_error;
  }
  return B_OK;
}

BString Header::describe() {
  BString result;
  result << "Request number: " << this->requestNumber
         << "; Body Length: " << this->bodyLength
         << "; End or error: " << this->endOrError()
         << "; Stream: " << this->stream() << "; Body type: ";
  switch (this->bodyType()) {
  case BodyType::BINARY:
    result << "binary";
    break;
  case BodyType::UTF8_STRING:
    result << "UTF-8 String";
    break;
  case BodyType::JSON:
    result << "JSON";
    break;
  default:
    result << "Unknown";
    break;
  }
  return result;
}

class RequestNameSink : public JSON::NodeSink {
public:
  RequestNameSink(std::vector<BString> *name);
  void addString(const BString &rawname, const BString &name,
                 const BString &raw, const BString &value) override;

private:
  std::vector<BString> *name;
};

class RequestObjectSink : public JSON::NodeSink {
public:
  RequestObjectSink(std::vector<BString> *name, RequestType *requestType,
                    BMessage *args);
  void addString(const BString &rawname, const BString &name,
                 const BString &raw, const BString &value) override;
  std::unique_ptr<JSON::NodeSink> addArray(const BString &rawname,
                                           const BString &name) override;

private:
  std::vector<BString> *name;
  RequestType *requestType;
  BMessage *args;
};

class RequestSink : public JSON::NodeSink {
public:
  RequestSink(std::vector<BString> *name, RequestType *requestType,
              BMessage *args);
  std::unique_ptr<JSON::NodeSink> addObject(const BString &rawname,
                                            const BString &name) override;

private:
  std::vector<BString> *name;
  RequestType *requestType;
  BMessage *args;
};

RequestObjectSink::RequestObjectSink(std::vector<BString> *name,
                                     RequestType *requestType, BMessage *args)
    :
    name(name),
    requestType(requestType),
    args(args) {}

RequestSink::RequestSink(std::vector<BString> *name, RequestType *requestType,
                         BMessage *args)
    :
    name(name),
    requestType(requestType),
    args(args) {}

std::unique_ptr<JSON::NodeSink> RequestSink::addObject(const BString &rawname,
                                                       const BString &name) {
  return std::make_unique<RequestObjectSink>(this->name, this->requestType,
                                             this->args);
}

void RequestObjectSink::addString(const BString &rawname, const BString &name,
                                  const BString &raw, const BString &value) {
  if (name == "type") {
    if (value == "source")
      *this->requestType = RequestType::SOURCE;
    else if (value == "duplex")
      *this->requestType = RequestType::DUPLEX;
    else if (value == "async")
      *this->requestType = RequestType::ASYNC;
    else
      *this->requestType = RequestType::UNKNOWN;
  }
}

std::unique_ptr<JSON::NodeSink>
RequestObjectSink::addArray(const BString &rawname, const BString &name) {
  if (name == "name")
    return std::make_unique<RequestNameSink>(this->name);
  else if (name == "args")
    return std::make_unique<JSON::BMessageArrayDocSink>(args);
  else
    return JSON::NodeSink::addArray(rawname, name);
}

RequestNameSink::RequestNameSink(std::vector<BString> *name)
    :
    name(name) {}

void RequestNameSink::addString(const BString &rawname, const BString &name,
                                const BString &raw, const BString &value) {
  this->name->push_back(value);
}

status_t Connection::readOne() {
  Header header;
  {
    status_t err;
    if ((err = this->populateHeader(&header)) != B_OK)
      return err;
  }
  if (status_t err = acquire_sem(this->ongoingLock); err != B_OK)
    return err;
  if (auto search = this->inboundOngoing.find(header.requestNumber);
      search != this->inboundOngoing.end()) {
    release_sem(this->ongoingLock);
    BMessage wrapper('MXRP');
    switch (header.bodyType()) {
    case BodyType::JSON: {
      {
        JSON::Parser parser(
            std::make_unique<JSON::BMessageObjectDocSink>(&wrapper), true);
        {
          BString name("content");
          parser.setPropName(name);
        }
        status_t result =
            JSON::parse(&parser, this->inner.get(), header.bodyLength);
        if (result != B_OK)
          return result;
      }
    } break;
    case BodyType::UTF8_STRING: {
      BString content;
      ssize_t remaining = header.bodyLength;
      while (remaining > 0) {
        char buffer[1024];
        ssize_t count = this->inner->Read(
            buffer,
            remaining > ((ssize_t)sizeof(buffer)) ? sizeof(buffer) : remaining);
        remaining -= count;
        if (count <= 0)
          return B_PARTIAL_READ;
        content.Append(buffer, count);
      }
      wrapper.AddString("content", content);
    } break;
    case BodyType::BINARY: {
      auto content = std::make_unique<unsigned char[]>(header.bodyLength);
      status_t result =
          this->inner->ReadExactly(content.get(), header.bodyLength);
      if (result != B_OK)
        return result;
      wrapper.AddData("content", B_RAW_TYPE, content.get(), header.bodyLength,
                      false);
    } break;
    }
    wrapper.AddBool("stream", header.stream());
    wrapper.AddBool("end", header.endOrError());
    wrapper.AddUInt32("sequence", search->second.sequence);
    BMessenger next = search->second.target;
    if (header.endOrError() || !header.stream()) {
      if (acquire_sem(this->ongoingLock) == B_OK) {
        this->inboundOngoing.erase(header.requestNumber);
        release_sem(this->ongoingLock);
      }
      this->Lock();
      for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
        if (auto handler = dynamic_cast<SenderHandler *>(this->HandlerAt(i));
            handler && handler->requestNumber == -header.requestNumber) {
          handler->canceled = true;
          BMessenger(handler).SendMessage('SEND');
          break;
        }
      }
      this->Unlock();
    }
    if (next.IsValid())
      return next.SendMessage(&wrapper);
    else
      return B_OK;
  } else {
    release_sem(this->ongoingLock);
    switch (header.bodyType()) {
    case BodyType::JSON: {
      std::vector<BString> name;
      RequestType requestType = RequestType::MISSING;
      BMessage args;
      {
        status_t result = JSON::parse(
            std::make_unique<RequestSink>(&name, &requestType, &args),
            this->inner.get(), header.bodyLength);
        if (result != B_OK)
          return result;
      }
      for (int i = 0; i < this->handlers->size(); i++) {
        MethodMatch match =
            (*this->handlers)[i]->check(this, name, requestType);
        switch (match) {
        case MethodMatch::NO_MATCH:
        case MethodMatch::WRONG_TYPE:
          break;
        case MethodMatch::MATCH:
          SenderHandler *replies = NULL;
          try {
            replies = new SenderHandler(this, -header.requestNumber);
          } catch (...) {
            if (replies != NULL)
              delete replies;
            throw;
          }
          this->Lock();
          this->AddHandler(replies);
          this->Unlock();
          BMessenger inbound;
          (*this->handlers)[i]->call(this, requestType, &args,
                                     BMessenger(replies), &inbound);
          if (header.stream() && !header.endOrError()) {
            if (acquire_sem(this->ongoingLock) == B_OK) {
              this->inboundOngoing.insert({header.requestNumber, {inbound, 1}});
              release_sem(this->ongoingLock);
            }
          }
          return B_OK;
        }
      }
      {
        SenderHandler *replies = new SenderHandler(this, -header.requestNumber);
        this->Lock();
        this->AddHandler(replies);
        this->Unlock();
        BMessage errorMessage('JSOB');
        BString errorText("method:");
        BString logText = this->cypherkey();
        logText << " called unknown method ";
        for (uint32 i = 0; i < name.size(); i++) {
          if (i > 0) {
            errorText << ",";
            logText << ".";
          }
          errorText << name[i];
          logText << name[i];
        }
        switch (requestType) {
        case RequestType::SOURCE:
          logText << " (source)";
          break;
        case RequestType::DUPLEX:
          logText << " (duplex)";
          break;
        case RequestType::ASYNC:
          logText << " (async)";
          break;
        case RequestType::MISSING:
          logText << " (type missing)";
          break;
        case RequestType::UNKNOWN:
          logText << " (type unrecognised)";
        }
        logText << " ";
        {
          JSON::RootSink rootSink(
              std::make_unique<JSON::SerializerStart>(&logText));
          JSON::fromBMessage(&rootSink, &args);
        }
        writeLog('MNOR', logText);
        errorText << " is not in the list of allowed methods";
        errorMessage.AddString("message", errorText);
        errorMessage.AddString("name", "error");
        Sender(BMessenger(replies))
            .send(&errorMessage, header.stream(), true, false);
        if (header.stream() && !header.endOrError()) {
          if (acquire_sem(this->ongoingLock) == B_OK) {
            BMessenger dummy;
            this->inboundOngoing.insert({header.requestNumber, {dummy, 1}});
            release_sem(this->ongoingLock);
          }
        }
      }
    } break;
    default: {
      // TODO: send error back
    } break;
    }
  }
  return B_OK;
}

BString Connection::cypherkey() {
  BString result("@");
  result << base64::encode(this->peer, crypto_sign_PUBLICKEYBYTES,
                           base64::STANDARD);
  result << ".ed25519";
  return result;
}

void Connection::addCloseHook(std::function<void()> hook) {
  this->cleanup.push_back(std::move(hook));
}

BodyType Header::bodyType() { return static_cast<BodyType>(this->flags & 3); }

bool Header::endOrError() { return (this->flags & 4) != 0; }

bool Header::stream() { return (this->flags & 8) != 0; }

void Header::setBodyType(BodyType value) {
  this->flags = (this->flags & ~3) | static_cast<int>(value);
}

void Header::setEndOrError(bool value) {
  if (value)
    this->flags |= 4;
  else
    this->flags &= ~4;
}

void Header::setStream(bool value) {
  if (value)
    this->flags |= 8;
  else
    this->flags &= ~8;
}

status_t Header::writeToBuffer(unsigned char *buffer) {
  status_t last_error;
  uint32 bodyLength(this->bodyLength);
  int32 requestNumber(this->requestNumber);
  *buffer = this->flags;
  if ((last_error = swap_data(B_UINT32_TYPE, &bodyLength, sizeof(uint32),
                              B_SWAP_HOST_TO_BENDIAN)) != B_OK) {
    return last_error;
  }
  memcpy(buffer + 1, &bodyLength, sizeof(int32));
  if ((last_error = swap_data(B_INT32_TYPE, &requestNumber, sizeof(int32),
                              B_SWAP_HOST_TO_BENDIAN)) != B_OK) {
    return last_error;
  }
  memcpy(buffer + 5, &requestNumber, sizeof(uint32));
  return B_OK;
}

bool MessageOrder::operator()(BMessage *&a, BMessage *&b) {
  uint32 aseq, bseq;
  if (a->FindUInt32("sequence", &aseq) != B_OK)
    aseq = 0;
  if (b->FindUInt32("sequence", &bseq) != B_OK)
    bseq = 0;
  return aseq > bseq;
}

MethodSuite::MethodSuite()
    :
    methods(std::make_shared<std::vector<std::shared_ptr<Method>>>(
        std::vector<std::shared_ptr<Method>>())),
    connectionHooks(
        std::make_shared<std::vector<std::shared_ptr<ConnectionHook>>>(
            std::vector<std::shared_ptr<ConnectionHook>>())) {}

MethodSuite::MethodSuite(const MethodSuite &original, bool includeHooks)
    :
    methods(original.methods),
    connectionHooks(
        includeHooks
            ? original.connectionHooks
            : std::make_shared<std::vector<std::shared_ptr<ConnectionHook>>>(
                  std::vector<std::shared_ptr<ConnectionHook>>())) {}

MethodSuite &MethodSuite::operator=(const MethodSuite &original) {
  this->methods = original.methods;
  this->connectionHooks = original.connectionHooks;
  return *this;
}

void MethodSuite::registerMethod(std::shared_ptr<Method> method) {
  this->methods->push_back(method);
}

void MethodSuite::registerConnectionHook(std::shared_ptr<ConnectionHook> call) {
  this->connectionHooks->push_back(call);
}

void MethodSuite::copyHooks(const MethodSuite &other) {
  for (auto &hook : *(other.connectionHooks))
    this->registerConnectionHook(hook);
}
}; // namespace muxrpc
