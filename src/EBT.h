#ifndef EBT_H
#define EBT_H

#include "MUXRPC.h"

namespace ebt {
struct Note {
  bool replicate;
  bool receive;
  uint64 sequence;
};

Note decodeNote(double note);

class Dispatcher : public BLooper {
public:
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void MessageReceived(BMessage *msg) override;
};

class Begin : public muxrpc::Method {
public:
  Begin(Dispatcher *dispatcher);
  ~Begin();

private:
  Dispatcher *dispatcher;
};
} // namespace ebt

#endif // EBT_H