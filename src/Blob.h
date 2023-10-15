#ifndef BLOB_H
#define BLOB_H

#include "MUXRPC.h"
#include <Query.h>
#include <Volume.h>
#include <tuple>
#include <vector>

namespace blob {
class Get : public muxrpc::Method {};

class GetSlice : public muxrpc::Method {};

class CreateWants : public muxrpc::Method {};

class Wanted : public BHandler {
public:
  void MessageReceived(BMessage *message) override;
  void addWant(BString &cypherkey, int8 distance,
               BMessenger replyTo = BMessenger());

private:
  std::vector<std::tuple<BString, int8, BQuery, std::vector<BMessenger>>>
      wanted;
  BVolume volume;
};
} // namespace blob

#endif