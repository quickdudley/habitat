#include "Room.h"
#include "Logging.h"

namespace rooms2 {
namespace {
class AttendantsClient : public BHandler {
public:
  void MessageReceived(BMessage *message) override;
};

void AttendantsClient::MessageReceived(BMessage *message) {
  {
    BMessage content;
    if (message->FindMessage("content", &content) != B_OK)
      goto cleanup;
    BString type;
    if (content.FindString("type", &type) != B_OK)
      goto cleanup;
    if (type == "state") {
      BMessage ids;
      if (content.FindMessage("ids", &ids) != B_OK)
        goto cleanup;
      BString logEntry("Initial room attendants: ");
      BString delimiter;
      BString id;
      BString ix;
      for (int i = 0; ix.SetTo(""), ix << i, ids.FindString(ix, &id) == B_OK;
           i++) {
        logEntry << delimiter;
        logEntry << id;
        delimiter.SetTo(",");
        writeLog('ROOM', logEntry);
      }
    } else if (type == "joined") {
      BString id;
      if (content.FindString("id", &id) != B_OK)
        goto cleanup;
      BString logEntry("Room attendant joined: ");
      logEntry << id;
      writeLog('ROOM', logEntry);
    } else {
      goto cleanup;
    }
  }
cleanup:
  if (message->GetBool("end", false) || !message->GetBool("stream", true)) {
    this->Looper()->RemoveHandler(this);
    delete this;
  }
}

class Stage1 : public BHandler {
public:
  void MessageReceived(BMessage *message) override;
};

void Stage1::MessageReceived(BMessage *message) {
  bool hasRoom2 = false;
  bool hasTunnel = false;
  {
    BMessage content;
    if (message->FindMessage("content", &content) != B_OK)
      goto cleanup;
    BMessage features;
    if (content.FindMessage("features", &features) != B_OK)
      goto cleanup;

    {
      BString feature;
      BString ix;
      for (int i = 0;
           ix.SetTo(""), ix << i, features.FindString(ix, &feature) == B_OK;
           i++) {
        if (feature == "room2")
          hasRoom2 = true;
        else if (feature == "tunnel")
          hasTunnel = true;
      }
    }
  }
  if (hasRoom2 && hasTunnel) {
    auto attendants = new AttendantsClient();
    this->Looper()->AddHandler(attendants);
    BMessage args('JSAR');
    dynamic_cast<muxrpc::Connection *>(this->Looper())
        ->request({"room", "attendants"}, muxrpc::RequestType::SOURCE, &args,
                  BMessenger(attendants), NULL);
  }
cleanup:
  this->Looper()->RemoveHandler(this);
  delete this;
}

class MetadataHook : public muxrpc::ConnectionHook {
public:
  void call(muxrpc::Connection *rpc) override;
};

void MetadataHook::call(muxrpc::Connection *rpc) {
  auto stage1 = new Stage1();
  rpc->AddHandler(stage1);
  BMessage args('JSAR');
  rpc->request({"room", "metadata"}, muxrpc::RequestType::ASYNC, &args,
               BMessenger(stage1), NULL);
}
} // namespace

void installClient(muxrpc::MethodSuite *suite) {
  suite->registerConnectionHook(std::make_shared<MetadataHook>());
}
} // namespace rooms2
