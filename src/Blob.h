#ifndef BLOB_H
#define BLOB_H

#include "MUXRPC.h"
#include "Post.h"
#include <Directory.h>
#include <Query.h>
#include <Volume.h>
#include <memory>
#include <queue>
#include <tuple>
#include <vector>

namespace blob {

class Wanted;

class LocalHandler : public BHandler {
public:
  LocalHandler(BMessage *original);
  void MessageReceived(BMessage *message) override;

private:
  std::unique_ptr<BMessage> original;
};

class Get : public muxrpc::Method {
public:
  Get(BLooper *looper, BVolume volume);
  status_t call(muxrpc::Connection *connection, muxrpc::RequestType type,
                BMessage *args, BMessenger replyTo,
                BMessenger *inbound) override;

private:
  BLooper *looper;
  BVolume volume;
};

class GetSlice : public muxrpc::Method {};

class Has : public muxrpc::Method {
public:
  Has(Wanted *wanted);
  status_t call(muxrpc::Connection *connection, muxrpc::RequestType type,
                BMessage *args, BMessenger replyTo,
                BMessenger *inbound) override;

private:
  Wanted *wanted;
};

class CreateWants : public muxrpc::Method {
public:
  CreateWants(Wanted *wanted);
  status_t call(muxrpc::Connection *connection, muxrpc::RequestType type,
                BMessage *args, BMessenger replyTo,
                BMessenger *inbound) override;

private:
  Wanted *wanted;
};

class Wanted : public QueryBacked {
public:
  Wanted(BDirectory dir);
  ~Wanted();
  void MessageReceived(BMessage *message) override;
  void addWant(BString &cypherkey, int8 distance,
               BMessenger replyTo = BMessenger());
  void pullWants(muxrpc::Connection *connection);
  void sendWants(BMessenger target);
  void propagateWant(BString &cypherkey, int8 distance);
  void registerMethods();
  status_t hashFile(entry_ref *ref);
  void sawSource(const BString &cypherkey, muxrpc::Connection *connection);
  bool fillQuery(BQuery *query, time_t reset) override;
  bool queryMatch(entry_ref *entry) override;

private:
  void _addWant_(BString &cypherkey, int8 distance,
                 BMessenger replyTo = BMessenger());
  status_t fetch(const BString &cypherkey, muxrpc::Connection *connection);
  std::vector<std::tuple<BString, int8, std::vector<BMessenger>,
                         std::queue<muxrpc::Connection *>>>
      wanted;
  BDirectory dir;
  BVolume volume;
};
} // namespace blob

#endif
