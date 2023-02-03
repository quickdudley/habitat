#include "MUXRPC.h"
#include <support/ByteOrder.h>
#include <utility>

namespace muxrpc {

Connection::Connection(std::unique_ptr<BDataIO> inner, unsigned char peer[32]) {
  this->inner = std::move(inner);
  memcpy(this->peer, peer, crypto_sign_PUBLICKEYBYTES);
}

status_t Connection::populateHeader(header *out) {
  unsigned char buffer[9];
  status_t last_error;
  if ((last_error = this->inner->ReadExactly(buffer, 9)) != B_OK) {
    return last_error;
  }
  out->flags = buffer[0];
  if ((last_error = swap_data(B_INT32_TYPE, buffer + 1, sizeof(uint32),
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
}; // namespace muxrpc
