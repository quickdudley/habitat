#ifndef EBT_H
#define EBT_H

#include "MUXRPC.h"
#include "Post.h"
#include <ctime>
#include <map>
#include <queue>
#include <set>

namespace ebt {
struct Note {
  bool replicate;
  bool receive;
  uint64 sequence;
};

Note decodeNote(double note);
double encodeNote(Note &note);

struct RemoteState {
  Note note;
  std::time_t updated;
  RemoteState(double note);
  RemoteState(const RemoteState &original);
};

class Dispatcher;

class Link : public BHandler {
public:
  Link(muxrpc::Sender sender);
  void MessageReceived(BMessage *message) override;

private:
  SSBDatabase *db();
  muxrpc::Sender sender;
  std::map<BString, RemoteState> remoteState;
  std::map<BString, Note> ourState;
  std::queue<BString> sendSequence;
  std::set<BString> unsent;
  friend class Dispatcher;
};

class Dispatcher : public BLooper {
public:
  Dispatcher(SSBDatabase *db);
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void MessageReceived(BMessage *msg) override;

private:
  void checkForMessage(const BString &author, uint64 sequence);
  SSBDatabase *db;
  friend class Link;
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