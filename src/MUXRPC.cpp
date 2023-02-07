#include "MUXRPC.h"
#include <support/ByteOrder.h>
#include <utility>

namespace muxrpc {

Connection::Connection(
    std::unique_ptr<BDataIO> inner,
    std::map<std::vector<BString>, std::unique_ptr<Endpoint>> *endpoints,
    unsigned char peer[32]) {
  this->inner = std::move(inner);
  this->endpoints = endpoints;
  memcpy(this->peer, peer, crypto_sign_PUBLICKEYBYTES);
}

status_t Connection::populateHeader(Header *out) {
  unsigned char buffer[9];
  status_t last_error;
  if ((last_error = this->inner->ReadExactly(buffer, 9)) != B_OK) {
    return last_error;
  }
  out->flags = buffer[0];
  if ((last_error = swap_data(B_UINT32_TYPE, buffer + 1, sizeof(uint32),
                              B_SWAP_BENDIAN_TO_HOST)) != B_OK) {
    return last_error;
  }
  memcpy(&(out->bodyLength), buffer + 5, sizeof(uint32));
  if ((last_error = swap_data(B_INT32_TYPE, buffer + 5, sizeof(uint32),
                              B_SWAP_BENDIAN_TO_HOST)) != B_OK) {
    return last_error;
  }
  memcpy(&(out->requestNumber), buffer + 5, sizeof(uint32));
  return B_OK;
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
}; // namespace muxrpc
