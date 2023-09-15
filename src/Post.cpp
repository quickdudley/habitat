#include "Post.h"
#include "BJSON.h"
#include "Base64.h"
#include "SignJSON.h"
#include <File.h>
#include <Path.h>
#include <PropertyInfo.h>
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
  BQuery query;
  this->store.GetVolume(&this->volume);
  query.SetVolume(&this->volume);
  query.PushAttr("HABITAT:author");
  BString attrValue = this->cypherkey();
  query.PushString(attrValue.String());
  query.PushOp(B_EQ);
  this->updateMessenger = BMessenger(this);
  if (query.Fetch() == B_OK) {
    entry_ref ref;
    while (query.GetNextRef(&ref) == B_OK) {
      BEntry entry(&ref);
      BNode node(&entry);
      int64 sequence;
      node.ReadAttr("HABITAT:sequence", B_INT64_TYPE, 0, &sequence,
                    sizeof(int64));
      if (sequence > this->lastSequence) {
        this->pending.push({sequence, ref});
      }
      while (!this->pending.empty() &&
             this->pending.top().sequence == this->lastSequence + 1) {
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
        memcpy(this->lastHash, &hash[0],
               std::min((size_t)crypto_sign_SECRETKEYBYTES, hash.size()));
      }
    }
  }
}

SSBFeed::~SSBFeed() {}

void SSBFeed::start() {
  this->updateMessenger = BMessenger(this);
  // I don't seem to receive any updates via live query; will just leave it
  // for now and try again later.
  this->updateQuery.SetVolume(&this->volume);
  this->updateQuery.PushAttr("HABITAT:author");
  BString attrValue = this->cypherkey();
  this->updateQuery.PushString(attrValue.String());
  this->updateQuery.PushOp(B_EQ);
  this->updateQuery.PushAttr("HABITAT:sequence");
  this->updateQuery.PushInt32(this->lastSequence);
  this->updateQuery.PushOp(B_GT);
  this->updateQuery.PushOp(B_AND);
  this->updateQuery.SetTarget(this->updateMessenger);
  this->updateQuery.Fetch();
  BEntry entry;
  for (; this->updateQuery.GetNextEntry(&entry) == B_OK;)
    ;
}

static property_info ssbFeedProperties[] = {{"Cypherkey",
                                             {B_GET_PROPERTY, 0},
                                             {B_DIRECT_SPECIFIER, 0},
                                             "The SSB identifier for this feed",
                                             0,
                                             {B_STRING_TYPE}},
                                            {0}};

status_t SSBFeed::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat-ssb-feed");
  BPropertyInfo propertyInfo(ssbFeedProperties);
  data->AddFlat("messages", &propertyInfo);
  return BHandler::GetSupportedSuites(data);
}

void SSBFeed::MessageReceived(BMessage *msg) {
  if (!msg->HasSpecifiers()) {
    msg->PrintToStream();
    return BHandler::MessageReceived(msg);
  }
  BMessage reply(B_REPLY);
  status_t error = B_ERROR;
  int32 index;
  BMessage specifier;
  int32 what;
  const char *property;
  if (msg->GetCurrentSpecifier(&index, &specifier, &what, &property) != B_OK)
    return BHandler::MessageReceived(msg);
  BPropertyInfo propertyInfo(ssbFeedProperties);
  switch (propertyInfo.FindMatch(msg, index, &specifier, what, property)) {
  case 0: // Cypherkey
    reply.AddString("result", this->cypherkey());
    error = B_OK;
    break;
  default:
    return BHandler::MessageReceived(msg);
  }
  reply.AddInt32("error", error);
  msg->SendReply(&reply);
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

BString SSBFeed::previousLink() {
  BString result("%");
  result.Append(base64::encode(this->lastHash, crypto_hash_sha256_BYTES,
                               base64::STANDARD));
  result.Append(".sha256");
  return result;
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
    if (this->lastSequence >= 0) {
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
    rootSink.addNumber(key, (JSON::number)std::time(NULL));
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
  if ((result = validateHash(message)) != B_OK)
    return result;
  if ((result = validateSequence(message, lastSequence)) != B_OK)
    return result;
  if ((result = validatePrevious(message, lastID)) != B_OK)
    return result;
  if ((result = validateOrder(message)) != B_OK)
    return result;
  if ((result = validateEitherContent(message)) != B_OK)
    return result;
  if ((result = validateOrder(message)) != B_OK)
    return result;
  if ((result = validateSize(message)) != B_OK)
    return result;
  if ((result = validateSignature(message, useHmac, hmacKey)) != B_OK)
    return result;
  return B_OK;
}
} // namespace post
