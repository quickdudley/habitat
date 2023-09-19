#include "MUXRPC.h"
#include "BJSON.h"
#include "JSON.h"
#include <support/ByteOrder.h>
#include <utility>

namespace muxrpc {

MethodMatch Method::check(unsigned char peer[crypto_sign_PUBLICKEYBYTES],
                          std::vector<BString> name, RequestType type) {
  if (name == this->name) {
    if (type == this->expectedType) {
      return MethodMatch::MATCH;
    } else {
      return MethodMatch::WRONG_TYPE;
    }
  } else {
    return MethodMatch::NO_MATCH;
  }
}

Sender::Sender(BMessenger inner)
    :
    inner(inner),
    sequenceSemaphore(create_sem(1, "MUXRPC packet ordering")) {}

SenderHandler::SenderHandler(Connection *conn, int32 requestNumber)
    :
    requestNumber(requestNumber) {
  conn->AddHandler(this);
}

Sender::~Sender() { delete_sem(this->sequenceSemaphore); }

SenderHandler::~SenderHandler() { this->Looper()->RemoveHandler(this); }

status_t Sender::send(BMessage *content, bool stream, bool error,
                      bool inOrder) {
  BMessage wrapper('SEND');
  wrapper.AddMessage("content", content);
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
  return this->inner.SendMessage(&wrapper);
}

status_t Sender::send(BString &content, bool stream, bool error, bool inOrder) {
  BMessage wrapper('SEND');
  wrapper.AddString("content", content);
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
  return this->inner.SendMessage(&wrapper);
}

status_t Sender::send(unsigned char *content, uint32 length, bool stream,
                      bool error, bool inOrder) {
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
  return this->inner.SendMessage(&wrapper);
}

void SenderHandler::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case 'SEND': {
    uint32 sequence;
    if (msg->FindUInt32("sequence", &sequence) == B_OK) {
      uint32 nextSequence = this->sentSequence + 1;
      if (sequence <= nextSequence) {
        this->sentSequence = sequence;
        nextSequence = sequence + 1;
        this->actuallySend(msg);
        while (!this->outOfOrder.empty() &&
               (sequence = this->outOfOrder.top().GetUInt32("sequence", 0)) <=
                   nextSequence) {
          this->sentSequence = sequence;
          nextSequence = sequence + 1;
          this->actuallySend(&this->outOfOrder.top());
          this->outOfOrder.pop();
        }
      } else {
        this->outOfOrder.push(*msg);
      }
    } else {
      this->actuallySend(msg);
    }
    break;
  }
  case 'DEL_':
    delete this;
    break;
  default:
    BHandler::MessageReceived(msg);
  }
}

void SenderHandler::actuallySend(const BMessage *wrapper) {
  Header header;
  header.setEndOrError(wrapper->GetBool("end", true));
  header.setStream(wrapper->GetBool("stream", true));
  header.requestNumber = this->requestNumber;
  {
    BString content;
    if (wrapper->FindString("content", &content) == B_OK) {
      header.setBodyType(BodyType::UTF8_STRING);
    } else {
      BMessage inner;
      if (wrapper->FindMessage("content", &inner) == B_OK) {
        header.setBodyType(BodyType::JSON);
        JSON::RootSink rootSink(
            std::make_unique<JSON::SerializerStart>(&content));
        JSON::fromBMessage(&rootSink, &inner);
      } else {
        goto check_for_raw;
      }
    }
    header.bodyLength = content.Length();
    BDataIO *output = this->output();
    unsigned char headerBytes[9];
    header.writeToBuffer(headerBytes);
    output->WriteExactly(headerBytes, 9);
    output->WriteExactly(content.String(), content.Length());
    return;
  }
check_for_raw : {
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
  }
}
}

BDataIO *SenderHandler::output() {
  return dynamic_cast<Connection *>(this->Looper())->inner.get();
}

Connection::Connection(std::unique_ptr<BDataIO> inner) {
  this->inner = std::move(inner);
  this->ongoingLock = create_sem(1, "MUXRPC incoming connections lock");
}

Connection::~Connection() { delete_sem(this->ongoingLock); }

thread_id Connection::Run() {
  this->pullThreadID =
      spawn_thread(Connection::pullThreadFunction, "MUXRPC receiver", 10, this);
  resume_thread(this->pullThreadID);
  return BLooper::Run();
}

void Connection::Quit() {
  BLooper::Quit();
  if (this->pullThreadID != B_NO_MORE_THREADS) {
    send_data(this->pullThreadID, 'STOP', NULL, 0);
    status_t exitValue;
    wait_for_thread(this->pullThreadID, &exitValue);
  }
  this->pullThreadID = B_NO_MORE_THREADS;
}

status_t Connection::populateHeader(Header *out) {
  unsigned char buffer[9];
  status_t last_error;
  if ((last_error = this->inner->ReadExactly(buffer, 9)) != B_OK) {
    return last_error;
  }
  return out->readFromBuffer(buffer);
}

int32 Connection::pullLoop() {
  status_t result;
  do {
    result = readOne();
    if (has_data(this->pullThreadID)) {
      thread_id sender;
      if (receive_data(&sender, NULL, 0) == 'STOP')
        return B_CANCELED;
    }
  } while (result == B_OK);
  return result;
}

int32 Connection::pullThreadFunction(void *data) {
  return ((Connection *)data)->pullLoop();
}

status_t Header::readFromBuffer(unsigned char *buffer) {
  status_t last_error;
  this->flags = buffer[0];
  if (this->flags & 3 == 3)
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

class RequestNameSink : public JSON::NodeSink {
public:
  RequestNameSink(std::vector<BString> *name);
  void addString(BString &rawname, BString &name, BString &raw,
                 BString &value) override;

private:
  std::vector<BString> *name;
};

class RequestObjectSink : public JSON::NodeSink {
public:
  RequestObjectSink(std::vector<BString> *name, RequestType *requestType,
                    BMessage *args);
  void addString(BString &rawname, BString &name, BString &raw,
                 BString &value) override;
  std::unique_ptr<JSON::NodeSink> addArray(BString &rawname,
                                           BString &name) override;

private:
  std::vector<BString> *name;
  RequestType *requestType;
  BMessage *args;
};

class RequestSink : public JSON::NodeSink {
public:
  RequestSink(std::vector<BString> *name, RequestType *requestType,
              BMessage *args);
  std::unique_ptr<JSON::NodeSink> addObject(BString &rawname,
                                            BString &name) override;

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

std::unique_ptr<JSON::NodeSink> RequestSink::addObject(BString &rawname,
                                                       BString &name) {
  return std::make_unique<RequestObjectSink>(this->name, this->requestType,
                                             this->args);
}

void RequestObjectSink::addString(BString &rawname, BString &name, BString &raw,
                                  BString &value) {
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

std::unique_ptr<JSON::NodeSink> RequestObjectSink::addArray(BString &rawname,
                                                            BString &name) {
  if (name == "name") {
    return std::make_unique<RequestNameSink>(this->name);
  } else if (name == "args") {
    return std::make_unique<JSON::BMessageDocSink>(args);
  } else {
    return JSON::NodeSink::addArray(rawname, name);
  }
}

RequestNameSink::RequestNameSink(std::vector<BString> *name)
    :
    name(name) {}

void RequestNameSink::addString(BString &rawname, BString &name, BString &raw,
                                BString &value) {
  this->name->push_back(value);
}

status_t Connection::readOne() {
  Header header;
  this->populateHeader(&header);
  // TODO: Use the semaphore
  if (auto search = this->inboundOngoing.find(header.requestNumber);
      search != this->inboundOngoing.end()) {
    BMessage wrapper('MXRP');
    switch (header.bodyType()) {
    case BodyType::JSON: {
      BMessage content;
      {
        status_t result =
            JSON::parse(std::make_unique<JSON::BMessageDocSink>(&content),
                        this->inner.get(), header.bodyLength);
        if (result != B_OK)
          return result;
      }
      wrapper.AddMessage("content", &content);
    } break;
    case BodyType::UTF8_STRING: {
      BString content;
      ssize_t remaining = header.bodyLength;
      while (remaining > 0) {
        char buffer[1024];
        ssize_t count = this->inner->Read(
            buffer, remaining > sizeof(buffer) ? sizeof(buffer) : remaining);
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
    return search->second.target.SendMessage(&wrapper);
  } else {
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
      MethodMatch overall = MethodMatch::NO_MATCH;
      for (int i = 0; i < this->handlers->size(); i++) {
        MethodMatch match =
            (*this->handlers)[i]->check(this->peer, name, requestType);
        switch (match) {
        case MethodMatch::NO_MATCH:
          break;
        case MethodMatch::WRONG_TYPE:
          overall = MethodMatch::WRONG_TYPE;
          break;
        case MethodMatch::MATCH:
          SenderHandler *replies;
          try {
            replies = new SenderHandler(this, -header.requestNumber);
          } catch (...) {
            delete replies;
            throw;
          }
          BMessenger inbound;
          status_t result = (*this->handlers)[i]->call(
              this->peer, requestType, &args, BMessenger(replies), &inbound);
          if (result == B_OK && requestType == RequestType::DUPLEX) {
            acquire_sem(this->ongoingLock);
            this->inboundOngoing.insert({header.requestNumber, {inbound, 1}});
            release_sem(this->ongoingLock);
          }
        }
      }
    } break;
    default:
      // TODO: send error back
      break;
    }
  }
  return B_OK;
}

BodyType Header::bodyType() { return static_cast<BodyType>(this->flags & 3); }

bool Header::endOrError() { return (this->flags & 4) != 0; }

bool Header::stream() { return (this->flags & 8) != 0; }

void Header::setBodyType(BodyType value) {
  this->flags = (this->flags & ~3) | static_cast<int>(value);
}

void Header::setEndOrError(bool value) {
  if (value) {
    this->flags |= 4;
  } else {
    this->flags &= ~4;
  }
}

void Header::setStream(bool value) {
  if (value) {
    this->flags |= 8;
  } else {
    this->flags &= ~8;
  }
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

bool MessageOrder::operator()(BMessage &a, BMessage &b) {
  uint32 aseq, bseq;
  if (a.FindUInt32("sequence", &aseq) != B_OK)
    aseq = 0;
  if (b.FindUInt32("sequence", &bseq) != B_OK)
    bseq = 0;
  return aseq > bseq;
}
}; // namespace muxrpc
