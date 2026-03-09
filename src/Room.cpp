#include "Room.h"
#include "Connection.h"
#include "Logging.h"
#include "Main.h"
#include "Tunnel.h"
#include <Application.h>
#include <cstring>

// TODO:
//   4. Put peers connected via other transports in the set too. (in progress)

namespace rooms2 {
namespace {
class AttendantsClient : public BHandler {
public:
  AttendantsClient();
  void MessageReceived(BMessage *message) override;
  void maybeConnect(std::set<BString> peers);
};

AttendantsClient::AttendantsClient() {}

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
      std::set<BString> peers;
      BString logEntry("Initial room attendants: ");
      BString delimiter;
      BString id;
      BString ix;
      for (int i = 0; ix.SetTo(""), ix << i, ids.FindString(ix, &id) == B_OK;
           i++) {
        peers.insert(id);
        logEntry << delimiter;
        logEntry << id;
        delimiter.SetTo(", ");
      }
      writeLog('ROOM', logEntry);
      this->maybeConnect(peers);
    } else if (type == "joined") {
      BString id;
      if (content.FindString("id", &id) != B_OK)
        goto cleanup;
      std::set<BString> peers;
      BString logEntry("Room attendant joined: ");
      logEntry << id;
      writeLog('ROOM', logEntry);
      peers.insert(id);
      this->maybeConnect(peers);
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

void AttendantsClient::maybeConnect(std::set<BString> peers) {
  // TODO: When outgoing LAN connections are supported use
  // `BMessageRunner::StartSending` to introduce a delay
  auto connectedList = ConnectedList::instance();
  {
    auto already = connectedList->getConnected();
    for (auto i = peers.begin(); i != peers.end();) {
      if (already.find(*i) == already.end())
        ++i;
      else
        peers.erase(i);
    }
  }
  auto conn = static_cast<muxrpc::Connection *>(this->Looper());
  auto portal = conn->cypherkey();
  for (auto &peer : peers) {
    connectedList->addConnected(peer);
    auto reader = new TunnelReader(NULL);
    BMessage arg0('JSOB');
    arg0.AddString("portal", portal);
    arg0.AddString("target", peer);
    BMessage args('JSAR');
    args.AddMessage("0", &arg0);
    conn->AddHandler(reader);
    BMessenger outbound;
    conn->request({"tunnel", "connect"}, muxrpc::RequestType::DUPLEX, &args,
                  reader, &outbound);
    auto tunnel = new Tunnel(outbound);
    reader->setSink(tunnel);
    static_cast<Habitat *>(be_app)->initiateConnection(tunnel, peer, [peer]() {
      ConnectedList::instance()->rmConnected(peer);
    });
  }
}

class Stage1 : public BHandler {
public:
  Stage1();
  void MessageReceived(BMessage *message) override;
};

Stage1::Stage1() {}

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
  MetadataHook();
  void call(muxrpc::Connection *rpc) override;
};

MetadataHook::MetadataHook() {}

void MetadataHook::call(muxrpc::Connection *rpc) {
  auto stage1 = new Stage1();
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
  std::function<void()> cleanupHook;
  if (BMessage arg0; args->FindMessage("0", &arg0) == B_OK) {
    if (BString origin; arg0.FindString("origin", &origin) == B_OK) {
      auto connectedList = ConnectedList::instance();
      connectedList->addConnected(origin);
      cleanupHook = [connectedList, origin]() {
        connectedList->rmConnected(origin);
      };
    }
  }
  auto tunnel = new Tunnel(replyTo);
  auto reader = new TunnelReader(tunnel);
  connection->Lock();
  connection->AddHandler(reader);
  connection->Unlock();
  *inbound = BMessenger(reader);
  dynamic_cast<Habitat *>(be_app)->acceptConnection(tunnel, cleanupHook);
  return B_OK;
}
} // namespace

void installClient(muxrpc::MethodSuite *suite) {
  ConnectedList::instance();
  suite->registerConnectionHook(std::make_shared<MetadataHook>());
  suite->registerMethod(std::make_shared<MkTunnel>());
}
} // namespace rooms2
