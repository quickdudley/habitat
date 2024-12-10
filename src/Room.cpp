#include "Room.h"
#include "Logging.h"
#include "Main.h"
#include "Tunnel.h"
#include <Application.h>

namespace rooms2 {
namespace {
class ConnectedList : public BHandler {
public:
  void MessageReceived(BMessage *message) override;

private:
  std::set<BString> connected;
};

void ConnectedList::MessageReceived(BMessage *message) {
  // TODO:
  //   1. Open connection when AttendantsClient sees a non-connected attendant.
  //   2a. Add attendants to the connected set when we proactively connect.
  //   2b. Add attendants to the connected set when they connect with us.
  //   3. Remove attendants from the connected set when the connection drops
  //      or fails.
  //   4. Put peers connected via other transports in the set too.
}

class AttendantsClient : public BHandler {
public:
  AttendantsClient(ConnectedList *connected);
  void MessageReceived(BMessage *message) override;

private:
  BMessenger connected;
};

AttendantsClient::AttendantsClient(ConnectedList *connected)
    :
    connected(connected) {}

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
        delimiter.SetTo(", ");
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
  Stage1(ConnectedList *connectedList);
  void MessageReceived(BMessage *message) override;

private:
  ConnectedList *connectedList;
};

Stage1::Stage1(ConnectedList *connectedList)
    :
    connectedList(connectedList) {}

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
    auto attendants = new AttendantsClient(this->connectedList);
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
  MetadataHook(ConnectedList *connectedList);
  void call(muxrpc::Connection *rpc) override;

private:
  ConnectedList *connectedList;
};

MetadataHook::MetadataHook(ConnectedList *connectedList)
    :
    connectedList(connectedList) {}

void MetadataHook::call(muxrpc::Connection *rpc) {
  auto stage1 = new Stage1(this->connectedList);
  rpc->AddHandler(stage1);
  BMessage args('JSAR');
  rpc->request({"room", "metadata"}, muxrpc::RequestType::ASYNC, &args,
               BMessenger(stage1), NULL);
}

class MkTunnel : public muxrpc::Method {
public:
  MkTunnel();
  status_t call(muxrpc::Connection *connection, muxrpc::RequestType type,
                BMessage *args, BMessenger replyTo,
                BMessenger *inbound) override;
};

MkTunnel::MkTunnel() {
  this->name = {"tunnel", "connect"};
  this->expectedType = muxrpc::RequestType::DUPLEX;
}

status_t MkTunnel::call(muxrpc::Connection *connection,
                        muxrpc::RequestType type, BMessage *args,
                        BMessenger replyTo, BMessenger *inbound) {
  auto tunnel = new Tunnel(replyTo);
  auto reader = new TunnelReader(tunnel);
  connection->Lock();
  connection->AddHandler(reader);
  connection->Unlock();
  *inbound = BMessenger(reader);
  dynamic_cast<Habitat *>(be_app)->acceptConnection(tunnel);
  return B_OK;
}
} // namespace

void installClient(muxrpc::MethodSuite *suite) {
  auto connectedList = new ConnectedList();
  be_app->Lock();
  be_app->AddHandler(connectedList);
  be_app->Unlock();
  suite->registerConnectionHook(std::make_shared<MetadataHook>(connectedList));
  suite->registerMethod(std::make_shared<MkTunnel>());
}
} // namespace rooms2
