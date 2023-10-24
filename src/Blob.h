#ifndef BLOB_H
#define BLOB_H

#include "MUXRPC.h"
#include <Query.h>
#include <Volume.h>
#include <memory>
#include <tuple>
#include <vector>

namespace blob {
class Wanted;

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

class Has : public muxrpc::Method {};

class CreateWants : public muxrpc::Method {
public:
  CreateWants(std::shared_ptr<Wanted> wanted);
  status_t call(muxrpc::Connection *connection, muxrpc::RequestType type,
                BMessage *args, BMessenger replyTo,
                BMessenger *inbound) override;

private:
  std::shared_ptr<Wanted> wanted;
};

class Wanted : public BHandler {
public:
  ~Wanted();
  void MessageReceived(BMessage *message) override;
  void addWant(BString &cypherkey, int8 distance,
               BMessenger replyTo = BMessenger());
  void pullWants(muxrpc::Connection *connection);
  void sendWants(BMessenger target);
  void propagateWant(BString &cypherkey, int8 distance);

private:
  std::vector<std::tuple<BString, int8, BQuery, std::vector<BMessenger>>>
      wanted;
  BVolume volume;
};
} // namespace blob

#endif