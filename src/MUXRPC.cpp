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

Sender::Sender(Connection *conn, int32 requestNumber)
    :
    requestNumber(requestNumber),
    sequenceSemaphore(create_sem(1, "MUXRPC packet ordering")) {
  conn->AddHandler(this);
}

Sender::~Sender() {
  this->Looper()->RemoveHandler(this);
  delete_sem(this->sequenceSemaphore);
}

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
    int32 sequence = this->sequence++;
    release_sem(this->sequenceSemaphore);
    wrapper.AddInt32("sequence", sequence);
  }
  return BMessenger(this).SendMessage(&wrapper);
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
    int32 sequence = this->sequence++;
    release_sem(this->sequenceSemaphore);
    wrapper.AddInt32("sequence", sequence);
  }
  return BMessenger(this).SendMessage(&wrapper);
}

status_t Sender::send(unsigned char *content, uint32 length, bool stream,
                      bool error, bool inOrder) {
  BMessage wrapper('SEND');
  wrapper.AddData("content", 'RAW_', content, length, false);
  wrapper.AddBool("stream", stream);
  wrapper.AddBool("end", error);
  if (inOrder) {
    status_t result;
    if ((result = acquire_sem(this->sequenceSemaphore)) < B_NO_ERROR)
      return result;
    int32 sequence = this->sequence++;
    release_sem(this->sequenceSemaphore);
    wrapper.AddInt32("sequence", sequence);
  }
  return BMessenger(this).SendMessage(&wrapper);
}

void Sender::MessageReceived(BMessage *msg) {}

BDataIO *Sender::output() {
  return dynamic_cast<Connection *>(this->Looper())->inner.get();
}

status_t Connection::populateHeader(Header *out) {
  unsigned char buffer[9];
  status_t last_error;
  if ((last_error = this->inner->ReadExactly(buffer, 9)) != B_OK) {
    return last_error;
  }
  return out->readFromBuffer(buffer);
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
  //	switch (header.bodyType()) {
  //		case BodyType::BINARY:
  //		break;
  //		case BodyType::UTF8_STRING:
  //		break;
  //		case BodyType::JSON:
  //		break;
  //	}
  if (auto search = this->inboundOngoing.find(header.requestNumber);
      search != this->inboundOngoing.end()) {
    // TODO: handle reply or stream continuation
    // construct message
    // send to search->second
  } else {
    switch (header.bodyType()) {
    case BodyType::JSON: {
      std::vector<BString> name;
      RequestType requestType = RequestType::MISSING;
      BMessage args;
      {
        JSON::Parser parser(
            std::make_unique<RequestSink>(&name, &requestType, &args));
        char buffer[1024];
        status_t result;
        ssize_t remaining = header.bodyLength;
        while (remaining > 0) {
          ssize_t count = this->inner->Read(
              buffer, remaining > sizeof(buffer) ? sizeof(buffer) : remaining);
          remaining -= count;
          if (count <= 0)
            return B_PARTIAL_READ;
          for (int i = 0; i < count; i++) {
            if ((result = parser.nextChar(buffer[i])) != B_OK)
              return result;
          }
        }
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
          // TODO: setup *ongoing, call handler
          return B_OK;
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
