#include "EBT.h"

namespace ebt {

Note decodeNote(double note) {
  bool replicate = note >= 0;
  uint64 v = note;
  bool receive = replicate && (v & 1) == 0;
  return {replicate, receive, replicate ? v >> 1 : 0};
}

status_t Dispatcher::GetSupportedSuites(BMessage *data) {
  // TODO
  return BLooper::GetSupportedSuites(data);
}

BHandler *Dispatcher::ResolveSpecifier(BMessage *msg, int32 index,
                                       BMessage *specifier, int32 what,
                                       const char *property) {
  // TODO
  return BLooper::ResolveSpecifier(msg, index, specifier, what, property);
}

void Dispatcher::MessageReceived(BMessage *msg) {
  // TODO
  return BLooper::MessageReceived(msg);
}

Begin::Begin(Dispatcher *dispatcher)
    :
    dispatcher(dispatcher) {
  this->expectedType = muxrpc::RequestType::DUPLEX;
  this->name.push_back("ebt");
  this->name.push_back("replicate");
}

Begin::~Begin() {}

} // namespace ebt