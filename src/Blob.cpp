#include "Blob.h"
#include "Base64.h"
#include "Listener.h"
#include "Logging.h"
#include <NodeMonitor.h>
#include <algorithm>
#include <iostream>
#include <sodium.h>

namespace blob {

Wanted::Wanted(BDirectory dir)
    :
    dir(dir) {
  this->dir.GetVolume(&this->volume);
}

Get::Get(BLooper *looper, BVolume volume)
    :
    looper(looper),
    volume(volume) {
  this->name = {"blobs", "get"};
  this->expectedType = muxrpc::RequestType::SOURCE;
}

namespace {
class GetSender : public BHandler {
public:
  GetSender(std::unique_ptr<BDataIO> source, BMessenger sink);
  void MessageReceived(BMessage *message);

private:
  std::unique_ptr<BDataIO> source;
  muxrpc::Sender sink;
};

GetSender::GetSender(std::unique_ptr<BDataIO> source, BMessenger sink)
    :
    source(std::move(source)),
    sink(sink) {}

void GetSender::MessageReceived(BMessage *message) {
  unsigned char chunk[65536];
  ssize_t read = this->source->Read(chunk, sizeof(chunk));
  if (read > 0) {
    this->sink.send(chunk, (uint32)read, true, false, true);
    BMessenger(this).SendMessage('TICK');
  } else {
    this->sink.send(true, true, true, true);
    BLooper *looper = this->Looper();
    looper->Lock();
    looper->RemoveHandler(this);
    looper->Unlock();
    delete this;
  }
}
} // namespace

status_t Get::call(muxrpc::Connection *connection, muxrpc::RequestType type,
                   BMessage *args, BMessenger replyTo, BMessenger *inbound) {
  BString cypherkey;
  int64 maxSize = INT64_MAX;
  {
    BMessage arg0;
    if (args->FindMessage("0", &arg0) == B_OK) {
      if (double size; arg0.FindDouble("max", &size))
        maxSize = (int64)size;
      if (arg0.FindString("key", &cypherkey) == B_OK)
        goto beginSend;
    }
  }
  if (args->FindString("0", &cypherkey) == B_OK) {
  beginSend:
    BQuery query;
    query.SetVolume(&this->volume);
    query.PushAttr("HABITAT:cypherkey");
    query.PushString(cypherkey.String());
    query.PushOp(B_EQ);
    if (query.Fetch() == B_OK) {
      entry_ref ref;
      if (query.GetNextRef(&ref) == B_OK) {
        {
          BNode node(&ref);
          off_t size;
          node.GetSize(&size);
          if (size > maxSize)
            goto failed;
        }
        auto sender =
            new GetSender(std::make_unique<BFile>(&ref, B_READ_ONLY), replyTo);
        this->looper->Lock();
        this->looper->AddHandler(sender);
        this->looper->Unlock();
        BMessenger(sender).SendMessage('TICK');
        return B_OK;
      }
    }
  }
failed : {
  BMessage reply('JSOB');
  reply.AddString("message", "could not get blob");
  reply.AddString("name", "Error");
  muxrpc::Sender(replyTo).send(&reply, true, true, false);
}
  return B_ERROR;
}

CreateWants::CreateWants(Wanted *wanted)
    :
    wanted(wanted) {
  this->name = {"blobs", "createWants"};
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
        if (node.ReadAttrString("HABITAT:cypherkey", &cypherkey) != B_OK)
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

namespace {
class WantSink : public BHandler {
public:
  WantSink(muxrpc::Connection *connection, Wanted *registry);
  void MessageReceived(BMessage *message) override;

private:
  muxrpc::Connection *connection;
  Wanted *registry;
};

class WantSource : public BHandler {
public:
  WantSource(BMessenger sender, Wanted *registry);
  void MessageReceived(BMessage *message) override;

private:
  muxrpc::Sender sender;
  Wanted *registry;
};

WantSink::WantSink(muxrpc::Connection *connection, Wanted *registry)
    :
    connection(connection),
    registry(registry) {}

WantSource::WantSource(BMessenger sender, Wanted *registry)
    :
    sender(sender),
    registry(registry) {}

void WantSink::MessageReceived(BMessage *message) {
  if (message->what == 'JSOB' || message->what == 'MXRP') {
    int32 index = 0;
    char *attrName;
    type_code attrType;
    status_t status;
    BMessage content;
    if (message->FindMessage("content", &content) != B_OK)
      goto cleanup;
    while ((status = content.GetInfo(B_DOUBLE_TYPE, index, &attrName,
                                     &attrType)) != B_BAD_INDEX) {
      // TODO: Make distance threshold user-configurable
      // TODO: For positive numbers, get the blob if we want it
      if (double distance; status == B_OK &&
                           content.FindDouble(attrName, &distance) == B_OK &&
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
        if (node.ReadAttrString("HABITAT:cypherkey", &cypherkey) != B_OK)
          return;
        BMessenger wantStream;
        status_t wsStatus;
        {
          BMessage getMessenger(B_GET_PROPERTY);
          getMessenger.AddSpecifier("CrossTalk", "WantedBlob");
          BMessage msgReply;
          BMessenger(this->connection).SendMessage(&getMessenger, &msgReply);
          wsStatus = msgReply.FindMessenger("result", &wantStream);
        }
        if (wsStatus == B_OK && wantStream.IsValid()) {
          BMessage toSend('HAVE');
          off_t size;
          if (node.GetSize(&size) != B_OK)
            return;
          toSend.AddString("cypherkey", cypherkey);
          toSend.AddInt64("size", size);
          wantStream.SendMessage(&toSend);
        }
        BMessage arg0('JSAR');
        arg0.AddString("0", cypherkey);
        BMessage args('JSAR');
        args.AddMessage("0", &arg0);
        std::vector<BString> name = {"blobs", "has"};
        this->connection->request(name, muxrpc::RequestType::ASYNC, &args,
                                  BMessenger(), NULL);
      }
    }
  } else {
    BHandler::MessageReceived(message);
  }
}

void WantSource::MessageReceived(BMessage *message) {
  switch (message->what) {
  case 'WANT': {
    BMessage forward('JSOB');
    BString blobID;
    int8 distance;
    if (message->FindString("cypherkey", &blobID) != B_OK)
      break;
    if (message->FindInt8("distance", &distance) != B_OK)
      break;
    forward.AddDouble(blobID, -(double)distance);
    this->sender.send(&forward, true, false, false);
  } break;
  case 'HAVE': {
    int64 size;
    BString blobID;
    if (message->FindString("cypherkey", &blobID) != B_OK)
      break;
    if (message->FindInt64("size", &size) != B_OK)
      break;
    BMessage forward('JSOB');
    forward.AddInt64(blobID.String(), size);
    this->sender.send(&forward, true, false, false);
  } break;
  }
  return BHandler::MessageReceived(message);
}
} // namespace

void Wanted::addWant(BString &cypherkey, int8 distance, BMessenger replyTo) {
  for (auto &item : this->wanted) {
    BString &existingKey = std::get<0>(item);
    if (existingKey == cypherkey) {
      if (distance < std::get<1>(item)) {
        std::get<1>(item) = distance;
        this->propagateWant(cypherkey, distance);
      }
      std::vector<BMessenger> &replyTargets = std::get<3>(item);
      std::remove_if(std::get<3>(item).begin(), std::get<3>(item).end(),
                     [&replyTo](auto existingTarget) {
                       return !existingTarget.IsValid() ||
                              existingTarget == replyTo;
                     });
      if (replyTo.IsValid())
        std::get<3>(item).push_back(replyTo);
      return;
    }
  }
  {
    this->wanted.push_back(std::tuple(BString(cypherkey), distance,
                                      std::make_unique<BQuery>(),
                                      std::vector<BMessenger>{replyTo}));
    BQuery &query = *std::get<2>(this->wanted.back());
    query.SetVolume(&this->volume);
    query.PushAttr("HABITAT:cypherkey");
    query.PushString(cypherkey.String());
    query.PushOp(B_EQ);
    if (this->Looper())
      query.SetTarget(BMessenger(this));
    status_t result;
    if ((result = query.Fetch()) == B_OK) {
      entry_ref ref;
      while (query.GetNextRef(&ref) == B_OK) {
        BMessage mimic(B_QUERY_UPDATE);
        mimic.AddInt32("opcode", B_ENTRY_CREATED);
        mimic.AddInt32("device", ref.device);
        mimic.AddInt64("directory", ref.directory);
        mimic.AddString("name", ref.name);
        BMessenger(this).SendMessage(&mimic);
      }
      this->propagateWant(cypherkey, distance);
    } else
      writeLog('QRST', strerror(result));
  }
}

status_t CreateWants::call(muxrpc::Connection *connection,
                           muxrpc::RequestType type, BMessage *args,
                           BMessenger replyTo, BMessenger *inbound) {
  BMessage createCrossTalk(B_CREATE_PROPERTY);
  createCrossTalk.AddSpecifier("CrossTalk");
  BLooper *looper = this->wanted->Looper();
  looper->Lock();
  WantSource *source;
  try {
    source = new WantSource(replyTo, this->wanted);
    looper->AddHandler(source);
  } catch (...) {
    looper->RemoveHandler(source);
    looper->Unlock();
    delete source;
    throw;
  }
  createCrossTalk.AddMessenger("messenger", BMessenger(source));
  createCrossTalk.AddString("name", "WantedBlob");
  *inbound = BMessenger(source);
  BMessenger(connection).SendMessage(&createCrossTalk);
  this->wanted->sendWants(BMessenger(source));
  looper->Unlock();
  // TODO: send IDs of blobs we have
  return B_OK;
}

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

void Wanted::sendWants(BMessenger target) {
  for (auto &want : this->wanted) {
    BMessage message('WANT');
    message.AddString("cypherkey", std::get<0>(want));
    message.AddInt8("distance", std::get<1>(want));
    target.SendMessage(&message);
  }
}

void Wanted::propagateWant(BString &cypherkey, int8 distance) {
  {
    BQuery query;
    query.SetVolume(&this->volume);
    query.PushAttr("HABITAT:cypherkey");
    query.PushString(cypherkey.String());
    query.PushOp(B_EQ);
    if (query.Fetch() != B_OK)
      goto propagate;
    entry_ref ref;
    if (query.GetNextRef(&ref) == B_OK)
      return;
  }
propagate:
  if (BLooper *looper = this->Looper(); looper) {
    std::vector<WantSource *> targets;
    looper->Lock();
    for (int32 i = looper->CountHandlers() - 1; i >= 0; i--) {
      if (WantSource *source = dynamic_cast<WantSource *>(looper->HandlerAt(i));
          source)
        targets.push_back(source);
    }
    looper->Unlock();
    BMessage message('WANT');
    message.AddString("cypherkey", cypherkey);
    message.AddInt8("distance", distance);
    for (auto target : targets)
      BMessenger(target).SendMessage(&message);
  }
}

status_t Wanted::hashFile(entry_ref *ref) {
  ssize_t readBytes;
  BFile file(ref, B_READ_ONLY);
  unsigned char buffer[1024];
  crypto_hash_sha256_state state;
  crypto_hash_sha256_init(&state);
  while ((readBytes = file.Read(buffer, sizeof(buffer))) > 0) {
    if (crypto_hash_sha256_update(&state, buffer, readBytes) < 0)
      return B_ERROR;
  }
  if (crypto_hash_sha256_final(&state, buffer) < 0)
    return B_ERROR;
  BString attr("&");
  attr.Append(
      base64::encode(buffer, crypto_hash_sha256_BYTES, base64::STANDARD));
  attr.Append(".sha256");
  return file.WriteAttrString("HABITAT:cypherkey", &attr);
}

void Wanted::registerMethods() {
  class CallCreateWants : public DefaultCall {
  public:
    CallCreateWants(Wanted *registry)
        :
        registry(registry) {}
    void call(muxrpc::Connection *rpc) { registry->pullWants(rpc); }

  private:
    Wanted *registry;
  };
  registerDefaultCall(std::make_shared<CallCreateWants>(this));
  registerMethod(std::make_shared<Get>(this->Looper(), this->volume));
  registerMethod(std::make_shared<CreateWants>(this));
}
} // namespace blob