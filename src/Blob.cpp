#include "Blob.h"
#include <NodeMonitor.h>
#include <algorithm>

namespace blob {

CreateWants::CreateWants(std::shared_ptr<Wanted> wanted)
    :
    wanted(wanted) {
  this->name = {"blob", "createWants"};
  this->expectedType = muxrpc::RequestType::SOURCE;
}

Wanted::~Wanted() {}

void Wanted::MessageReceived(BMessage *message) {
  switch (message->what) {
  case B_QUERY_UPDATE: {
    if (message->GetInt32("opcode", B_ERROR) == B_ENTRY_CREATED) {
      entry_ref ref;
      ref.device = message->GetInt32("device", B_ERROR);
      ref.directory = message->GetInt64("directory", B_ERROR);
      BString name;
      if (message->FindString("name", &name) == B_OK)
        ref.set_name(name.String());
      BEntry entry(&ref);
      if (entry.InitCheck() == B_OK && entry.Exists()) {
        BNode node(&ref);
        BString cypherkey;
        if (node.ReadAttrString("cypherkey", &cypherkey) != B_OK)
          return;
        for (auto item = this->wanted.begin(); item != this->wanted.end();
            item++) {
          if (std::get<0>(*item) == cypherkey) {
            for (auto &target : std::get<3>(*item))
              target.SendMessage(message);
            this->wanted.erase(item);
            return;
          }
        }
      }
    }
  } break;
  }
}

void Wanted::addWant(BString &cypherkey, int8 distance, BMessenger replyTo) {
  for (auto &item : this->wanted) {
    BString &existingKey = std::get<0>(item);
    if (existingKey == cypherkey) {
      std::get<1>(item) = std::min(std::get<1>(item), distance);
      std::vector<BMessenger> &replyTargets = std::get<3>(item);
      std::remove_if(
          std::get<3>(item).begin(), std::get<3>(item).end(),
          [](auto existingTarget) { return !existingTarget.IsValid(); });
      if (replyTo.IsValid())
        std::get<3>(item).push_back(replyTo);
      return;
    }
  }
  {
    BQuery query;
    query.SetVolume(&this->volume);
    query.PushAttr("HABITAT:cypherkey");
    query.PushString(cypherkey.String());
    query.PushOp(B_EQ);
    if (this->Looper())
      query.SetTarget(BMessenger(this));
    // TODO: Handle the case where we already have the blob.
    if (query.Fetch() == B_OK)
      this->wanted.push_back({cypherkey, distance, query, {replyTo}});
  }
}

namespace {
class WantSink : public BHandler {
public:
  WantSink(muxrpc::Connection *connection, Wanted *registry);
  void MessageReceived(BMessage *message) override;

private:
  muxrpc::Connection *connection;
  Wanted *registry;
};

WantSink::WantSink(muxrpc::Connection *connection, Wanted *registry)
    :
    connection(connection),
    registry(registry) {}

void WantSink::MessageReceived(BMessage *message) {
  if (message->what == 'MXRP') {
    int32 index = 0;
    char *attrName;
    type_code attrType;
    status_t status;
    BMessage content;
    if (message->FindMessage("content", &content) != B_OK)
      goto cleanup;
    while ((status = message->GetInfo(B_DOUBLE_TYPE, index, &attrName,
                                      &attrType)) != B_BAD_INDEX) {
      // TODO: Make distance threshold user-configurable
      if (double distance; status == B_OK &&
                           message->FindDouble(attrName, &distance) == B_OK &&
                           distance < 0 && distance >= -2) {
        BString cypherkey(attrName);
        this->registry->addWant(cypherkey, (int8)(-distance) + 1,
                                BMessenger(this));
      }
      index++;
    }
  cleanup:
    if (!message->GetBool("stream", true) || message->GetBool("end", false)) {
      BLooper *looper = this->Looper();
      looper->Lock();
      looper->RemoveHandler(this);
      looper->Unlock();
      delete this;
      return;
    }
  } else if (message->what == B_QUERY_UPDATE) {
    if (message->GetInt32("opcode", B_ERROR) == B_ENTRY_CREATED) {
      entry_ref ref;
      ref.device = message->GetInt32("device", B_ERROR);
      ref.directory = message->GetInt64("directory", B_ERROR);
      BString name;
      if (message->FindString("name", &name) == B_OK)
        ref.set_name(name.String());
      BEntry entry(&ref);
      if (entry.InitCheck() == B_OK && entry.Exists()) {
        BNode node(&ref);
        BString cypherkey;
        if (node.ReadAttrString("cypherkey", &cypherkey) != B_OK)
          return;
        BMessage arg0('JSAR');
        arg0.AddString("0", cypherkey);
        BMessage args('JSAR');
        args.AddMessage("0", &arg0);
        std::vector<BString> name = {"blobs", "has"};
        this->connection->request(name, muxrpc::RequestType::ASYNC, &args,
                                  BMessenger(this), NULL);
      }
    }
  } else
    BHandler::MessageReceived(message);
}
} // namespace

void Wanted::pullWants(muxrpc::Connection *connection) {
  std::vector<BString> name = {"blobs", "createWants"};
  WantSink *sink = new WantSink(connection, this);
  BLooper *looper = this->Looper();
  looper->Lock();
  looper->AddHandler(sink);
  looper->Unlock();
  BMessenger outbound;
  BMessage args('JSAR');
  connection->request(name, muxrpc::RequestType::SOURCE, &args,
                      BMessenger(sink), &outbound);
}
} // namespace blob