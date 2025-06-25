#ifndef EBT_H
#define EBT_H

#include "MUXRPC.h"
#include "Post.h"
#include <map>
#include <queue>
#include <random>
#include <set>

namespace ebt {
struct Note {
  bool replicate;
  bool receive;
  uint64 sequence;
  uint64 savedSequence;
};

Note decodeNote(double note);
int64 encodeNote(Note &note);

struct RemoteState {
  Note note;
  bigtime_t updated;
  RemoteState(double note);
  RemoteState(const RemoteState &original);
};

struct LocalState {
  uint64 sequence;
  uint64 savedSequence;
  uint64 forked; // TODO: Rename everywhere
};

struct LinkLocalState {
  bool replicate;
  bool receive;
};

class Dispatcher;

class Link : public BHandler {
public:
  Link(muxrpc::Sender sender, bool waiting = false);
  void MessageReceived(BMessage *message) override;
  void loadState();

private:
  SSBDatabase *db();
  void tick(const BString &author);
  void stopWaiting();
  BMessenger *outbound();
  void pushOut(BMessage *message);
  void sendOne();
  muxrpc::Sender sender;
  std::map<BString, RemoteState> remoteState;
  std::map<BString, LinkLocalState> ourState;
  std::queue<BString> sendSequence;
  std::map<BString, int64> lastSent;
  std::map<BString, std::queue<BMessage>> outMessages;
  bool waiting;
  bool sending = false;
  friend class Dispatcher;
};

class Dispatcher : public BLooper {
public:
  Dispatcher(SSBDatabase *db);
  thread_id Run() override;
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void MessageReceived(BMessage *msg) override;
  void Quit() override;
  void initiate(muxrpc::Connection *connection);

private:
  void noticeChange(BMessage *msg);
  void checkForMessage(const BString &author, uint64 sequence);
  void startNotesTimer(bigtime_t delay);
  void sendNotes();
  bool polyLink();
  std::map<BString, LocalState> ourState;
  SSBDatabase *db;
  std::minstd_rand rng;
  bool buildingNotes = false;
  bool clogged = false;
  friend class Link;
};

class Begin : public muxrpc::Method, public muxrpc::ConnectionHook {
public:
  Begin(Dispatcher *dispatcher);
  ~Begin();
  status_t call(muxrpc::Connection *connection, muxrpc::RequestType type,
                BMessage *args, BMessenger replyTo,
                BMessenger *inbound) override;
  void call(muxrpc::Connection *connection) override;

private:
  Dispatcher *dispatcher;
};
} // namespace ebt

#endif // EBT_H
