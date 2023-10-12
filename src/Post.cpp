#include "Post.h"
#include "BJSON.h"
#include "Base64.h"
#include "SignJSON.h"
#include <File.h>
#include <Path.h>
#include <cstring>
#include <ctime>
#include <unicode/utf8.h>
#include <vector>

BString messageCypherkey(unsigned char hash[crypto_hash_sha256_BYTES]) {
  BString result("%");
  BString body =
      base64::encode(hash, crypto_hash_sha256_BYTES, base64::STANDARD);
  result.Append(body);
  result.Append(".sha256");
  return result;
}

SSBDatabase::SSBDatabase(BDirectory store)
    :
    BLooper("SSB message database"),
    store(store) {}

SSBDatabase::~SSBDatabase() {}

enum { kReplicatedFeed, kAReplicatedFeed, kPostByID };

property_info databaseProperties[] = {
    {"ReplicatedFeed",
     {B_GET_PROPERTY, B_CREATE_PROPERTY, 'USUB', 0},
     {B_DIRECT_SPECIFIER, 0},
     "A known SSB log",
     kReplicatedFeed,
     {}},
    {"ReplicatedFeed",
     {},
     {B_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0},
     "A known SSB log",
     kAReplicatedFeed,
     {}},
    {"Post",
     {B_GET_PROPERTY, 0},
     {B_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0},
     "An SSB message",
     kPostByID,
     {}},
    {0}};

status_t SSBDatabase::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat+ssbdb");
  BPropertyInfo propertyInfo(databaseProperties);
  data->AddFlat("messages", &propertyInfo);
  return BLooper::GetSupportedSuites(data);
}

BHandler *SSBDatabase::ResolveSpecifier(BMessage *msg, int32 index,
                                        BMessage *specifier, int32 what,
                                        const char *property) {
  status_t error = B_OK;
  BPropertyInfo propertyInfo(databaseProperties);
  uint32 match;
  if (propertyInfo.FindMatch(msg, index, specifier, what, property, &match) >=
      0) {
    switch (match) {
    case kReplicatedFeed:
      return this;
    case kAReplicatedFeed: {
      BString name;
      int32 sindex;
      error = specifier->FindString("name", &name);
      if (error == B_OK) {
        SSBFeed *feed;
        error = this->findFeed(feed, name);
        if (error == B_OK && feed != NULL) {
          msg->PopSpecifier();
          BMessenger(feed).SendMessage(msg);
          return NULL;
        }
        error = B_NAME_NOT_FOUND;
      } else if ((error = specifier->FindInt32("index", &sindex)) == B_OK) {
        for (int32 i = 0, j = 0; i < this->CountHandlers(); i++) {
          SSBFeed *feed = dynamic_cast<SSBFeed *>(this->HandlerAt(i));
          if (feed) {
            if (j++ == sindex) {
              msg->PopSpecifier();
              BMessenger(feed).SendMessage(msg);
              return NULL;
            }
          }
        }
        error = B_BAD_INDEX;
      } else
        break;
    } break;
    default:
      return this;
    }
  } else {
    BString author;
    if (msg->FindString("author", &author) == B_OK) {
      SSBFeed *feed;
      error = this->findFeed(feed, author);
      if (error == B_OK)
        return feed;
    }
  }
  if (error == B_OK)
    return BLooper::ResolveSpecifier(msg, index, specifier, what, property);
  else {
    BMessage reply(B_MESSAGE_NOT_UNDERSTOOD);
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    msg->SendReply(&reply);
    return NULL;
  }
}

void SSBDatabase::MessageReceived(BMessage *msg) {
  if (msg->HasSpecifiers()) {
    BMessage reply(B_REPLY);
    status_t error = B_ERROR;
    int32 index;
    BMessage specifier;
    int32 what;
    const char *property;
    uint32 match;
    if (msg->GetCurrentSpecifier(&index, &specifier, &what, &property) != B_OK)
      return BLooper::MessageReceived(msg);
    BPropertyInfo propertyInfo(databaseProperties);
    propertyInfo.FindMatch(msg, index, &specifier, what, property, &match);
    switch (match) {
    case kReplicatedFeed:
      switch (msg->what) {
      case B_COUNT_PROPERTIES:
        // TODO
        error = B_UNSUPPORTED;
        break;
      case B_CREATE_PROPERTY: {
        unsigned char key[crypto_sign_PUBLICKEYBYTES];
        BString formatted;
        error = msg->FindString("cypherkey", &formatted);
        if (error == B_OK &&
            (error = SSBFeed::parseAuthor(key, formatted)) == B_OK) {
          SSBFeed *feed;
          if (this->findFeed(feed, formatted) != B_OK)
            feed = new SSBFeed(this->store, key);
          this->AddHandler(feed);
          feed->load();
          reply.AddMessenger("result", BMessenger(feed));
        }
      } break;
      case B_GET_PROPERTY: {
        for (int32 i = this->CountHandlers() - 1; i > 0; i--) {
          SSBFeed *feed = dynamic_cast<SSBFeed *>(this->HandlerAt(i));
          if (feed) {
            error = B_OK;
            BMessage oneResult;
            oneResult.AddString("cypherkey", feed->cypherkey());
            oneResult.AddUInt64("sequence", feed->sequence());
            reply.AddMessage("result", &oneResult);
          }
        }
      } break;
      case 'USUB': {
        BMessenger target;
        if ((error = msg->FindMessenger("subscriber", &target)) != B_OK)
          break;
        // StartWatching operates in the opposite direction than expected
        // so I'm using knownledge of Haiku internals.
        {
          BMessage obs('_OBS');
          obs.AddMessenger("be:observe_target", target);
          obs.AddInt32(B_OBSERVE_WHAT_CHANGE, 'NMSG');
          BHandler::MessageReceived(&obs);
        }
        for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
          SSBFeed *feed = dynamic_cast<SSBFeed *>(this->HandlerAt(i));
          if (feed != NULL) {
            feed->notifyChanges(target);
          }
        }
      } break;
      default:
        error = B_DONT_DO_THAT;
      }
      break;
    default:
      return BLooper::MessageReceived(msg);
    }
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    msg->SendReply(&reply);
    return;
  } else if (BString author; msg->FindString("author", &author) == B_OK) {
    SSBFeed *feed;
    if (this->findFeed(feed, author) == B_OK) {
      BMessenger(feed).SendMessage(msg);
    }
    return;
  }
  return BLooper::MessageReceived(msg);
}

status_t SSBDatabase::findFeed(SSBFeed *&result, BString &cypherkey) {
  for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
    SSBFeed *feed = dynamic_cast<SSBFeed *>(this->HandlerAt(i));
    if (feed && feed->cypherkey() == cypherkey) {
      result = feed;
      return B_OK;
    }
  }
  return B_NAME_NOT_FOUND;
}

status_t SSBDatabase::findPost(BMessage *post, BString &cypherkey) {
  status_t error;
  BQuery query;
  BVolume volume;
  this->store.GetVolume(&volume);
  query.SetVolume(&volume);
  query.PushAttr("HABITAT:cypherkey");
  query.PushString(cypherkey.String());
  query.PushOp(B_EQ);
  if ((error = query.Fetch()) != B_OK)
    return error;
  entry_ref ref;
  if (query.GetNextRef(&ref) != B_OK)
    return B_ENTRY_NOT_FOUND;
  BFile result(&ref, B_READ_ONLY);
  post->Unflatten(&result);
  return B_OK;
}

static inline status_t eitherNumber(int64 *result, BMessage *source,
                                    const char *name) {
  if (source->FindInt64(name, result) == B_OK) {
    return B_OK;
  } else {
    JSON::number parsed;
    if (source->FindDouble(name, &parsed) == B_OK) {
      *result = parsed;
      return B_OK;
    } else {
      return B_NAME_NOT_FOUND;
    }
  }
}

bool post_private_::FeedBuildComparator::operator()(const FeedShuntEntry &l,
                                                    const FeedShuntEntry &r) {
  return l.sequence > r.sequence;
}

SSBFeed::SSBFeed(BDirectory store,
                 unsigned char key[crypto_sign_PUBLICKEYBYTES])
    :
    BHandler(),
    store(store) {
  memcpy(this->pubkey, key, crypto_sign_PUBLICKEYBYTES);
  this->store.GetVolume(&this->volume);
}

status_t SSBFeed::load() {
  status_t error;
  this->query.Clear();
  this->query.SetVolume(&this->volume);
  this->query.PushAttr("HABITAT:author");
  BString attrValue = this->cypherkey();
  this->query.PushString(attrValue.String());
  this->query.PushOp(B_EQ);
  if (this->Looper()) {
    // TODO: Ensure the messages we get from this are handled, including
    // call to SendNotices
    this->query.SetTarget(BMessenger(this));
  }
  if ((error = this->query.Fetch()) == B_OK) {
    entry_ref ref;
    while (this->query.GetNextRef(&ref) == B_OK) {
      BNode node(&ref);
      int64 sequence;
      node.ReadAttr("HABITAT:sequence", B_INT64_TYPE, 0, &sequence,
                    sizeof(int64));
      if (sequence > this->lastSequence) {
        this->pending.push({sequence, ref});
      }
      while (!this->pending.empty() &&
             this->pending.top().sequence == this->lastSequence + 1) {
        sequence = this->pending.top().sequence;
        BEntry processingEntry(&this->pending.top().ref);
        BNode processingNode(&processingEntry);
        BString cypherkey;
        processingNode.ReadAttrString("HABITAT:cypherkey", &cypherkey);
        this->pending.pop();
        BString b64;
        // TODO: Make sure the cypherkey starts with '%' and ends with '.sha256'
        for (int i = 1; i < cypherkey.Length() && cypherkey[i] != '.'; i++) {
          b64.Append(cypherkey[i], 1);
        }
        std::vector<unsigned char> hash =
            base64::decode(b64.String(), b64.Length());
        this->lastSequence = sequence;
        memcpy(this->lastHash, &hash[0],
               std::min((size_t)crypto_sign_SECRETKEYBYTES, hash.size()));
      }
    }
  }
  this->notifyChanges();
  return error;
}

SSBFeed::~SSBFeed() {}

void SSBFeed::notifyChanges(BMessenger target) {
  BMessage notif(B_OBSERVER_NOTICE_CHANGE);
  notif.AddString("feed", this->cypherkey());
  notif.AddInt64("sequence", this->sequence());
  target.SendMessage(&notif);
}

void SSBFeed::notifyChanges() {
  BMessage notif(B_OBSERVER_NOTICE_CHANGE);
  notif.AddString("feed", this->cypherkey());
  notif.AddInt64("sequence", this->sequence());
  this->Looper()->SendNotices('NMSG', &notif);
}

enum {
  kFeedCypherkey,
  kFeedLastSequence,
  kFeedLastID,
  kOnePost,
};

static property_info ssbFeedProperties[] = {
    {"Cypherkey",
     {B_GET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "The SSB identifier for this feed",
     kFeedCypherkey,
     {B_STRING_TYPE}},
    {"Sequence",
     {B_GET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "The sequence number of the last known message in this feed, equal to the "
     "number of messages",
     kFeedLastSequence,
     {B_INT64_TYPE}},
    {"Last",
     {B_GET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "The Message ID of the last known message of this feed",
     kFeedLastID,
     {B_STRING_TYPE}},
    {"Post",
     {B_GET_PROPERTY, 0},
     {B_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0},
     "An SSB message",
     kOnePost,
     {}},
    {0},
};

status_t SSBFeed::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat+ssb-feed");
  BPropertyInfo propertyInfo(ssbFeedProperties);
  data->AddFlat("messages", &propertyInfo);
  return BHandler::GetSupportedSuites(data);
}

void SSBFeed::MessageReceived(BMessage *msg) {
  if (msg->HasSpecifiers()) {

    BMessage reply(B_REPLY);
    status_t error = B_ERROR;
    int32 index;
    BMessage specifier;
    int32 what;
    const char *property;
    uint32 match;
    if (msg->GetCurrentSpecifier(&index, &specifier, &what, &property) !=
        B_OK) {
      if (msg->what == B_DELETE_PROPERTY) {
        this->Looper()->RemoveHandler(this);
        delete this;
        error = B_OK;
      } else {
        return BHandler::MessageReceived(msg);
      }
    }
    BPropertyInfo propertyInfo(ssbFeedProperties);
    if (propertyInfo.FindMatch(msg, index, &specifier, what, property, &match) <
        0)
      return BHandler::MessageReceived(msg);
    switch (match) {
    case kFeedCypherkey:
      reply.AddString("result", this->cypherkey());
      error = B_OK;
      break;
    case kFeedLastSequence:
      reply.AddInt64("result", this->lastSequence);
      error = B_OK;
      break;
    case kFeedLastID:
      reply.AddString("result", this->previousLink());
      error = B_OK;
      break;
    case kOnePost: {
      int32 index;
      BMessage specifier;
      int32 spWhat;
      const char *property;
      if ((error = msg->GetCurrentSpecifier(&index, &specifier, &spWhat,
                                            &property)) != B_OK)
        break;
      if (spWhat == B_INDEX_SPECIFIER) {
        if ((error = specifier.FindInt32("index", &index)) != B_OK)
          break;
        BMessage post;
        if ((error = this->findPost(&post, index)) != B_OK)
          break;
        reply.AddMessage("result", &post);
      }
    } break;
    default:
      return BHandler::MessageReceived(msg);
    }
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    msg->SendReply(&reply);
  } else if (BString author; msg->FindString("author", &author) == B_OK &&
                             author == this->cypherkey()) {
    BString lastID = this->lastSequence == 0 ? "" : this->previousLink();
    BString blank;
    // TODO: Enqueue any that we get out of order.
    if (post::validate(msg, this->lastSequence, lastID, false, blank) == B_OK) {
      BMessage reply;
      if (this->save(msg, &reply) == B_OK) {
        msg->SendReply(&reply);
        return;
      }
    }
  } else {
    return BHandler::MessageReceived(msg);
  }
}

status_t SSBFeed::findPost(BMessage *post, uint64 sequence) {
  status_t error;
  BQuery query;
  query.SetVolume(&this->volume);
  query.PushAttr("HABITAT:author");
  query.PushString(this->cypherkey().String());
  query.PushOp(B_EQ);
  query.PushAttr("HABITAT:sequence");
  query.PushUInt64(sequence);
  query.PushOp(B_EQ);
  query.PushOp(B_AND);
  if ((error = query.Fetch()) != B_OK)
    return error;
  entry_ref ref;
  if (query.GetNextRef(&ref) != B_OK)
    return B_ENTRY_NOT_FOUND;
  BFile result(&ref, B_READ_ONLY);
  post->Unflatten(&result);
  return B_OK;
}

BHandler *SSBFeed::ResolveSpecifier(BMessage *msg, int32 index,
                                    BMessage *specifier, int32 what,
                                    const char *property) {
  msg->PrintToStream();
  BPropertyInfo propertyInfo(ssbFeedProperties);
  if (propertyInfo.FindMatch(msg, index, specifier, what, property) >= 0)
    return this;
  return BHandler::ResolveSpecifier(msg, index, specifier, what, property);
}

BString SSBFeed::cypherkey() {
  BString result("@");
  result.Append(base64::encode(this->pubkey, crypto_sign_PUBLICKEYBYTES,
                               base64::STANDARD));
  result.Append(".ed25519");
  return result;
}

int64 SSBFeed::sequence() { return this->lastSequence; }

BString SSBFeed::previousLink() {
  BString result("%");
  result.Append(base64::encode(this->lastHash, crypto_hash_sha256_BYTES,
                               base64::STANDARD));
  result.Append(".sha256");
  return result;
}

status_t SSBFeed::parseAuthor(unsigned char out[crypto_sign_PUBLICKEYBYTES],
                              BString &in) {
  // TODO: Also parse URL format
  if (in.StartsWith("@") && in.EndsWith(".ed25519")) {
    BString substring;
    in.CopyInto(substring, 1, in.Length() - 9);
    auto raw = base64::decode(substring);
    if (raw.size() == crypto_sign_PUBLICKEYBYTES) {
      memcpy(out, raw.data(), crypto_sign_PUBLICKEYBYTES);
      return B_OK;
    }
  }
  return B_ERROR;
}

status_t SSBFeed::save(BMessage *message, BMessage *reply) {
  status_t status;
  unsigned char msgHash[crypto_hash_sha256_BYTES];
  {
    JSON::RootSink rootSink(std::make_unique<JSON::Hash>(msgHash));
    JSON::fromBMessage(&rootSink, message);
  }
  BFile sink;
  BString filename =
      base64::encode(msgHash, crypto_hash_sha256_BYTES, base64::URL);
  if ((status = this->store.CreateFile(filename.String(), &sink, false)) !=
      B_OK)
    return status;
  if ((status = message->Flatten(&sink)) != B_OK)
    return status;
  BString attrString = messageCypherkey(msgHash);
  if ((status = sink.WriteAttrString("HABITAT:cypherkey", &attrString)) != B_OK)
    return status;
  BMessage result;
  entry_ref ref;
  BEntry entry(&this->store, filename.String());
  entry.GetRef(&ref);
  result.AddRef("ref", &ref);
  result.AddString("cypherkey", attrString);
  attrString = this->cypherkey();
  if ((status = sink.WriteAttrString("HABITAT:author", &attrString)) != B_OK)
    return status;
  int64 attrNum;
  if (eitherNumber(&attrNum, message, "sequence") == B_OK) {
    if (sink.WriteAttr("HABITAT:sequence", B_INT64_TYPE, 0, &attrNum,
                       sizeof(int64)) != sizeof(int64))
      return B_IO_ERROR;
  }
  memcpy(this->lastHash, msgHash, crypto_hash_sha256_BYTES);
  this->lastSequence = attrNum;
  this->notifyChanges();
  if (eitherNumber(&attrNum, message, "timestamp") == B_OK) {
    if (sink.WriteAttr("HABITAT:timestamp", B_INT64_TYPE, 0, &attrNum,
                       sizeof(int64)) != sizeof(int64))
      return B_IO_ERROR;
  }
  if (reply != NULL) {
    reply->AddMessage("result", &result);
  }
  return B_OK;
}

OwnFeed::OwnFeed(BDirectory store, Ed25519Secret *secret)
    :
    SSBFeed(store, secret->pubkey) {
  memcpy(this->seckey, secret->secret, crypto_sign_SECRETKEYBYTES);
}

static property_info ownFeedProperties[] = {{"Post",
                                             {B_CREATE_PROPERTY, 0},
                                             {B_DIRECT_SPECIFIER, 0},
                                             "Create a post on our own feed",
                                             0,
                                             {B_MESSAGE_TYPE}},
                                            {0}};

status_t OwnFeed::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat-own-feed");
  BPropertyInfo propertyInfo(ownFeedProperties);
  data->AddFlat("messages", &propertyInfo);
  return SSBFeed::GetSupportedSuites(data);
}

void OwnFeed::MessageReceived(BMessage *msg) {
  if (!msg->HasSpecifiers())
    return SSBFeed::MessageReceived(msg);
  BMessage reply(B_REPLY);
  status_t error = B_ERROR;
  int32 index;
  BMessage specifier;
  int32 what;
  const char *property;
  if (msg->GetCurrentSpecifier(&index, &specifier, &what, &property) != B_OK)
    return SSBFeed::MessageReceived(msg);
  BPropertyInfo propertyInfo(ownFeedProperties);
  switch (propertyInfo.FindMatch(msg, index, &specifier, what, property)) {
  case 0: // Create post
    error = this->create(msg, &reply);
    break;
  default:
    return SSBFeed::MessageReceived(msg);
  }
  reply.AddInt32("error", error);
  reply.AddString("message", strerror(error));
  msg->SendReply(&reply);
}

BHandler *OwnFeed::ResolveSpecifier(BMessage *msg, int32 index,
                                    BMessage *specifier, int32 what,
                                    const char *property) {
  BPropertyInfo propertyInfo(ownFeedProperties);
  if (propertyInfo.FindMatch(msg, index, specifier, what, property) >= 0)
    return this;
  return SSBFeed::ResolveSpecifier(msg, index, specifier, what, property);
}

status_t OwnFeed::create(BMessage *message, BMessage *reply) {
  class SignRoot : public JSON::NodeSink {
  public:
    SignRoot(BMessage *target, unsigned char key[crypto_sign_SECRETKEYBYTES])
        :
        target(target),
        key(key) {}
    std::unique_ptr<JSON::NodeSink> addObject(BString &rawname, BString &name) {
      return std::make_unique<JSON::SignObject>(
          std::make_unique<JSON::BMessageObjectDocSink>(this->target),
          this->key);
    }

  private:
    BMessage *target;
    unsigned char *key;
  };
  BMessage full;
  {
    JSON::RootSink rootSink(std::make_unique<SignRoot>(&full, this->seckey));
    BString key;
    rootSink.beginObject(key);
    key.SetTo("previous");
    BString value;
    if (this->lastSequence > 0) {
      value = this->previousLink();
      rootSink.addString(key, value);
    } else {
      rootSink.addNull(key);
    }
    key.SetTo("author");
    value = this->cypherkey();
    rootSink.addString(key, value);
    key.SetTo("sequence");
    rootSink.addNumber(key, (JSON::number)(++this->lastSequence));
    key.SetTo("timestamp");
    rootSink.addNumber(key, (JSON::number)std::time(NULL) * 1000);
    key.SetTo("hash");
    value.SetTo("sha256");
    rootSink.addString(key, value);
    key.SetTo("content");
    rootSink.beginObject(key);
    JSON::fromBMessageObject(&rootSink, message);
    rootSink.closeNode();
    rootSink.closeNode();
  }
  full.PrintToStream();
  return this->save(&full, reply);
}

namespace post {

static inline status_t validateSignature(BMessage *message, bool useHMac,
                                         BString &hmacKey) {
  bool signatureValid;
  {
    JSON::RootSink rootSink(
        useHMac
            ? std::make_unique<JSON::VerifySignature>(&signatureValid, hmacKey)
            : std::make_unique<JSON::VerifySignature>(&signatureValid));
    JSON::fromBMessage(&rootSink, message);
  }
  if (signatureValid)
    return B_OK;
  else
    return B_NOT_ALLOWED;
}

static inline status_t validateSequence(BMessage *message, int lastSequence) {
  double sequence;
  if (message->FindDouble("sequence", &sequence) != B_OK) {
    return B_NOT_ALLOWED;
  }
  if (!((lastSequence <= 0 && sequence == 1) ||
        (int(sequence) - 1 == lastSequence))) {
    return B_NOT_ALLOWED;
  }
  return B_OK;
}

static inline status_t validatePrevious(BMessage *message, BString &lastID) {
  if (lastID != "") {
    BString previous;
    if (message->FindString("previous", &previous) != B_OK)
      return B_NOT_ALLOWED;
    if (previous != lastID)
      return B_NOT_ALLOWED;
    return B_OK;
  } else {
    const void *data;
    ssize_t numBytes;
    if (message->FindData("previous", 'NULL', &data, &numBytes) != B_OK) {
      return B_NOT_ALLOWED;
    }
    return B_OK;
  }
  return B_NOT_ALLOWED;
}

static inline status_t validateHash(BMessage *message) {
  BString hash;
  if (message->FindString("hash", &hash) != B_OK || hash != "sha256") {
    return B_NOT_ALLOWED;
  }
  return B_OK;
}

static inline status_t validateContent(BMessage *content) {
  BString type;
  if (content->FindString("type", &type) != B_OK)
    return B_NOT_ALLOWED;
  if (type.Length() < 3 || type.Length() > 52)
    return B_NOT_ALLOWED;
  return B_OK;
}

static inline status_t validateContent(BString &content) {
  if (!(content.EndsWith(".box2") || content.EndsWith(".box")))
    return B_NOT_ALLOWED;
  return B_OK;
}

static inline status_t validateEitherContent(BMessage *message) {
  BMessage content;
  if (message->FindMessage("content", &content) == B_OK) {
    return validateContent(&content);
  } else {
    BString encrypted;
    if (message->FindString("content", &encrypted) == B_OK) {
      return validateContent(encrypted);
    } else {
      return B_NOT_ALLOWED;
    }
  }
}

static inline status_t validateOrder(BMessage *message) {
  char *attrname;
  type_code attrtype;
  int32 index = 0;
  int32 state = 0;
  while (message->GetInfo(B_ANY_TYPE, index, &attrname, &attrtype) == B_OK) {
    if (state == 0) {
      if (BString("previous") == attrname)
        state = 1;
      else
        return B_NOT_ALLOWED;
    } else if (state == 1) {
      if (BString("author") == attrname)
        state = 2;
      else if (BString("sequence") == attrname)
        state = 3;
      else
        return B_NOT_ALLOWED;
    } else if (state == 2) {
      if (BString("sequence") == attrname)
        state = 4;
      else
        return B_NOT_ALLOWED;
    } else if (state == 3) {
      if (BString("author") == attrname)
        state = 4;
      else
        return B_NOT_ALLOWED;
    } else if (state == 4) {
      if (BString("timestamp") == attrname)
        state = 5;
      else
        return B_NOT_ALLOWED;
    } else if (state == 5) {
      if (BString("hash") == attrname)
        state = 6;
      else
        return B_NOT_ALLOWED;
    } else if (state == 6) {
      if (BString("content") == attrname)
        state = 7;
      else
        return B_NOT_ALLOWED;
    } else if (state == 7) {
      if (BString("signature") == attrname)
        state = 8;
      else
        return B_NOT_ALLOWED;
    } else {
      return B_NOT_ALLOWED;
    }
    index++;
  }
  return B_OK;
}

static inline status_t validateSize(BMessage *message) {
  BString serialized;
  {
    JSON::RootSink rootSink(
        std::make_unique<JSON::SerializerStart>(&serialized));
    JSON::fromBMessage(&rootSink, message);
  }
  const char *u8 = serialized.String();
  uint32 codepoint, offset = 0;
  uint32 tally = 0;
  while (offset < serialized.Length()) {
    U8_NEXT_UNSAFE(u8, offset, codepoint);
    if (codepoint >= 0x10000)
      tally += 2;
    else
      tally++;
    if (tally > 8192)
      return B_NOT_ALLOWED;
  }
  return B_OK;
}

status_t validate(BMessage *message, int lastSequence, BString &lastID,
                  bool useHmac, BString &hmacKey) {
  status_t result;
#define CHECK(c)                                                               \
  if ((result = c) != B_OK)                                                    \
  return result
  CHECK(validateHash(message));
  CHECK(validateSequence(message, lastSequence));
  CHECK(validatePrevious(message, lastID));
  CHECK(validateOrder(message));
  CHECK(validateEitherContent(message));
  CHECK(validateSize(message));
  CHECK(validateSignature(message, useHmac, hmacKey));
#undef CHECK
  return B_OK;
}
} // namespace post
