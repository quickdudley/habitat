#include "Post.h"
#include "BJSON.h"
#include "Base64.h"
#include "SignJSON.h"
#include <File.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <StringList.h>
#include <cstring>
#include <ctime>
#include <iostream>
#include <unicode/utf8.h>
#include <vector>

namespace {
status_t postAttrs(BNode *sink, BMessage *message,
                   const unsigned char *prehashed = NULL);

class AttrCheck : public BHandler {
public:
  AttrCheck(BDirectory dir);
  void MessageReceived(BMessage *msg);

private:
  BDirectory dir;
};

AttrCheck::AttrCheck(BDirectory dir)
    :
    dir(dir) {
  this->dir.Rewind();
}

void AttrCheck::MessageReceived(BMessage *msg) {
  if (msg->what == B_PULSE) {
    BMessage tick(B_PULSE);
    if (BEntry entry; this->dir.GetNextEntry(&entry, true) == B_OK) {
      BFile file(&entry, B_READ_ONLY);
      if (BMessage contents;
          file.IsReadable() && contents.Unflatten(&file) == B_OK) {
        postAttrs(&file, &contents);
      }
      BMessenger(this).SendMessage(&tick);
      //  BMessageRunner::StartSending(this, &tick, 250000, 1);
    } else {
      this->dir.Rewind();
      BMessageRunner::StartSending(this, &tick, 86400000000, 1); // 24 hours
    }
  } else {
    BHandler::MessageReceived(msg);
  }
}

class QueryHandler : public QueryBacked {
public:
  QueryHandler(BMessenger target, const BMessage &specifier);
  void MessageReceived(BMessage *message) override;
  bool fillQuery(BQuery *query, time_t reset) override;
  bool queryMatch(entry_ref *entry) override;
  int32 limit = -1;

private:
  BMessenger target;
  BMessage specifier;
  bool started = false;
  bool ongoing = false;
  bool drips = false;
};

QueryHandler::QueryHandler(BMessenger target, const BMessage &specifier)
    :
    target(target),
    specifier(specifier) {}

status_t populateQuery(BQuery &query, BMessage *specifier) {
  bool already = false;
  bool disj = false;
#define CONJ()                                                                 \
  if (already)                                                                 \
    query.PushOp(B_AND);                                                       \
  else                                                                         \
    already = true;                                                            \
  disj = false
#define DISJ()                                                                 \
  if (disj)                                                                    \
    query.PushOp(B_OR);                                                        \
  else                                                                         \
    disj = true
  if (BString cypherkey;
      specifier->FindString(specifier->what == 'CPLX' ? "cypherkey" : "name",
                            &cypherkey) == B_OK) {
    query.PushAttr("HABITAT:cypherkey");
    query.PushString(cypherkey.String());
    query.PushOp(B_EQ);
    CONJ();
  }
#define STRC(mname, attr)                                                      \
  if (BStringList mname##s;                                                    \
      specifier->FindStrings(#mname, &mname##s) == B_OK) {                     \
    for (int32 i = mname##s.CountStrings() - 1; i >= 0; i--) {                 \
      query.PushAttr(attr);                                                    \
      query.PushString(mname##s.StringAt(i));                                  \
      query.PushOp(B_EQ);                                                      \
      DISJ();                                                                  \
    }                                                                          \
    if (disj) {                                                                \
      CONJ();                                                                  \
    }                                                                          \
  }
  STRC(author, "HABITAT:author")
  STRC(context, "HABITAT:context")
  STRC(type, "HABITAT:type")
  if (uint64 earliest; specifier->FindUInt64("earliest", &earliest) == B_OK) {
    query.PushAttr("HABITAT:timestamp");
    query.PushUInt64(earliest);
    query.PushOp(B_GE);
    CONJ();
  }
  if (uint64 latest; specifier->FindUInt64("latest", &latest) == B_OK) {
    query.PushAttr("HABITAT:timestamp");
    query.PushUInt64(latest);
    query.PushOp(B_LE);
    CONJ();
  }
  return already ? B_OK : B_BAD_VALUE;
#undef STRC
#undef DISJ
#undef CONJ
}

void QueryHandler::MessageReceived(BMessage *message) {
  this->ongoing = true;
  switch (message->what) {
  case B_QUERY_UPDATE:
    if (message->GetInt32("opcode", B_ERROR) == B_ENTRY_CREATED) {
      entry_ref ref;
      ref.device = message->GetInt32("device", B_ERROR);
      ref.directory = message->GetInt64("directory", B_ERROR);
      BString name;
      if (message->FindString("name", &name) == B_OK)
        ref.set_name(name.String());
      BFile file(&ref, B_READ_ONLY);
      BMessage post;
      if (post.Unflatten(&file) == B_OK) {
        if (this->target.IsValid())
          this->target.SendMessage(&post);
        else
          goto canceled;
        if (this->limit > 0 && --this->limit == 0)
          goto canceled;
      }
    }
    break;
  case 'DONE':
    if (this->started && !this->drips) {
      this->drips = true;
      this->target.SendMessage('DONE');
    } else {
      this->started = true;
    }
    if (this->target.IsValid())
      break;
  case B_QUIT_REQUESTED:
  canceled: {
    BLooper *looper = this->Looper();
    looper->Lock();
    looper->RemoveHandler(this);
    looper->Unlock();
    delete this;
  } break;
  }
}

bool QueryHandler::fillQuery(BQuery *query, time_t reset) {
  if (populateQuery(*query, &this->specifier) != B_OK)
    return false;
  if (this->ongoing) {
    query->PushAttr("last_modified");
    query->PushInt64((int64)reset);
    query->PushOp(B_GE);
    query->PushOp(B_AND);
  } else {
    this->ongoing = true;
  }
  return true;
}

bool QueryHandler::queryMatch(entry_ref *entry) {
  BNode node(entry);
  bool specifierHas;
  bool found;
  BString value;
  BString foundValue;
#define CKS(specKey, attrKey)                                                  \
  found = false;                                                               \
  foundValue = false;                                                          \
  for (int32 i = 0; this->specifier.FindString(specKey, i, &value) == B_OK;    \
    i++) {                                                                     \
    specifierHas = true;                                                       \
    if (i == 0) {                                                              \
      if (node.ReadAttrString(attrKey, &foundValue) != B_OK)                   \
        return false;                                                          \
    }                                                                          \
    if (value == foundValue) {                                                 \
      found = true;                                                            \
      break;                                                                   \
    }                                                                          \
  }                                                                            \
  if (specifierHas && !found)                                                  \
    return false;                                                              \
  specifierHas = false
  CKS(this->specifier.what == 'CPLX' ? "cypherkey" : "name",
      "HABITAT:cypherkey");
  CKS("author", "HABITAT:author");
  CKS("context", "HABITAT:context");
  CKS("type", "HABITAT:type");
#undef CKS
  if (uint64 earliest; this->specifier.FindUInt64("earliest", &earliest) == 0) {
    int64 timestamp;
    if (node.ReadAttr("HABITAT:timestamp", B_INT64_TYPE, 0, &timestamp,
                      sizeof(int64)) != B_OK) {
      return false;
    }
    if ((uint64)timestamp < earliest)
      return false;
  }
  if (uint64 latest; this->specifier.FindUInt64("earliest", &latest) == 0) {
    int64 timestamp;
    if (node.ReadAttr("HABITAT:timestamp", B_INT64_TYPE, 0, &timestamp,
                      sizeof(int64)) != B_OK) {
      return false;
    }
    if ((uint64)timestamp < latest)
      return false;
  }
  return true;
}
} // namespace

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
    store(store) {
  BHandler *check = new AttrCheck(store);
  this->AddHandler(check);
  BMessenger(check).SendMessage(B_PULSE);
}

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
     {B_NAME_SPECIFIER, 'CPLX', 0},
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
      } else {
        break;
      }
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
  if (error == B_OK) {
    return BLooper::ResolveSpecifier(msg, index, specifier, what, property);
  } else {
    BMessage reply(B_MESSAGE_NOT_UNDERSTOOD);
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    if (msg->ReturnAddress().IsValid())
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
          if (feed != NULL)
            feed->notifyChanges(target);
        }
      } break;
      default:
        error = B_DONT_DO_THAT;
      }
      break;
    case kPostByID: {
      QueryHandler *qh;
      bool live;
      if (BMessenger target; msg->FindMessenger("target", &target) == B_OK) {
        qh = new QueryHandler(target, specifier);
        live = true;
      } else {
        qh = new QueryHandler(BMessenger(), specifier);
        live = false;
      }
      qh->limit = msg->GetInt32("limit", -1);
      if (live) {
        this->Lock();
        this->AddHandler(qh);
        this->Unlock();
        if (!this->pendingQueryMods) {
          this->pendingQueryMods = true;
          BMessenger(this).SendMessage(B_PULSE);
        }
        error = B_OK;
      } else {
        error = B_ENTRY_NOT_FOUND;
        entry_ref ref;
        BQuery query;
        {
          BVolume volume;
          this->store.GetVolume(&volume);
          query.SetVolume(&volume);
        }
        qh->fillQuery(&query, 0);
        query.Fetch();
        while (qh->limit != 0 && query.GetNextRef(&ref) == B_OK) {
          error = B_OK;
          BMessage post;
          BFile file(&ref, B_READ_ONLY);
          if (post.Unflatten(&file) == B_OK) {
            reply.AddMessage("result", &post);
            if (qh->limit > 0)
              qh->limit--;
          }
        }
        delete qh;
      }
    } break;
    default:
      return BLooper::MessageReceived(msg);
    }
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    if (msg->ReturnAddress().IsValid())
      msg->SendReply(&reply);
    return;
  } else if (BString author; msg->FindString("author", &author) == B_OK) {
    SSBFeed *feed;
    if (this->findFeed(feed, author) == B_OK)
      BMessenger(feed).SendMessage(msg);
    return;
  } else if (msg->what == B_PULSE) {
    if (entry_ref entry; this->commonQuery.GetNextRef(&entry) == B_OK) {
      BMessage mimic(B_QUERY_UPDATE);
      mimic.AddInt32("opcode", B_ENTRY_CREATED);
      mimic.AddInt32("device", entry.device);
      mimic.AddInt64("directory", entry.directory);
      mimic.AddString("name", entry.name);
      BMessenger(this).SendMessage(&mimic);
      BMessenger(this).SendMessage(B_PULSE);
    } else if (this->pendingQueryMods) {
      time_t reset = time(NULL);
      this->commonQuery.Clear();
      this->commonQuery.SetTarget(this);
      {
        BVolume volume;
        this->store.GetVolume(&volume);
        this->commonQuery.SetVolume(&volume);
      }
      bool nonEmpty = false;
      for (int32 i = this->CountHandlers() - 1; i > 0; i--) {
        if (auto handler = dynamic_cast<QueryBacked *>(this->HandlerAt(i))) {
          BMessenger(handler).SendMessage('DONE');
          bool flag = handler->fillQuery(&this->commonQuery, reset);
          if (flag && nonEmpty)
            this->commonQuery.PushOp(B_OR);
          nonEmpty = nonEmpty || flag;
        }
      }
      this->pendingQueryMods = false;
      this->commonQuery.Fetch();
      if (nonEmpty)
        BMessenger(this).SendMessage(B_PULSE);
    }
  } else if (msg->what == 'UQRY') {
    if (!this->pendingQueryMods) {
      this->pendingQueryMods = true;
      BMessenger(this).SendMessage(B_PULSE);
    }
  } else if (msg->what == B_QUERY_UPDATE &&
             msg->GetInt32("opcode", B_ERROR) == B_ENTRY_CREATED) {
    entry_ref ref;
    ref.device = msg->GetInt32("device", B_ERROR);
    ref.directory = msg->GetInt64("directory", B_ERROR);
    if (BString name; msg->FindString("name", &name) == B_OK)
      ref.set_name(name.String());
    for (int32 i = this->CountHandlers() - 1; i > 0; i--) {
      if (auto handler = dynamic_cast<QueryBacked *>(this->HandlerAt(i))) {
        if (handler->queryMatch(&ref))
          BMessenger(handler).SendMessage(msg);
      }
    }
  } else {
    return BLooper::MessageReceived(msg);
  }
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
    QueryBacked(),
    store(store) {
  memcpy(this->pubkey, key, crypto_sign_PUBLICKEYBYTES);
  this->store.GetVolume(&this->volume);
}

status_t SSBFeed::load() {
  status_t error;
  BQuery query;
  query.SetVolume(&this->volume);
  query.PushAttr("HABITAT:author");
  BString attrValue = this->cypherkey();
  query.PushString(attrValue.String());
  query.PushOp(B_EQ);
  if (this->Looper()) {
    // TODO: Ensure the messages we get from this are handled, including
    // call to SendNotices
    query.SetTarget(BMessenger(this));
  }
  if ((error = query.Fetch()) == B_OK) {
    entry_ref ref;
    while (query.GetNextRef(&ref) == B_OK) {
      BNode node(&ref);
      int64 sequence;
      node.ReadAttr("HABITAT:sequence", B_INT64_TYPE, 0, &sequence,
                    sizeof(int64));
      if (sequence > this->lastSequence)
        this->pending.push({sequence, ref});
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
        for (int i = 1; i < cypherkey.Length() && cypherkey[i] != '.'; i++)
          b64.Append(cypherkey[i], 1);
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
        0) {
      return BHandler::MessageReceived(msg);
    }
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
                                            &property)) != B_OK) {
        break;
      }
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
    if (msg->ReturnAddress().IsValid())
      msg->SendReply(&reply);
  } else if (BString author; msg->FindString("author", &author) == B_OK &&
             author == this->cypherkey()) {
    BString lastID = this->lastSequence == 0 ? "" : this->previousLink();
    BString blank;
    // TODO: Enqueue any that we get out of order.
    if (post::validate(msg, this->lastSequence, lastID, false, blank) == B_OK) {
      BMessage reply;
      if (this->save(msg, &reply) == B_OK) {
        if (msg->ReturnAddress().IsValid())
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

bool SSBFeed::fillQuery(BQuery *query, time_t reset) {
  // TODO
  return false;
}

bool SSBFeed::queryMatch(entry_ref *entry) {
  // TODO
  return false;
}

namespace {
// TODO: Use something more flexible
const char *contextAttrs[] = {"link", "fork", "root", "project", "repo"};

status_t contextLink(BString *out, BString &type, BMessage *message) {
  for (int i = -1; i < (int)(sizeof(contextAttrs) / sizeof(char *)); i++) {
    const char *attrName = i >= 0 ? contextAttrs[i] : type.String();
    if (BString value; message->FindString(attrName, &value) == B_OK) {
      *out = value;
      return B_OK;
    } else if (BMessage inner; message->FindMessage(attrName, &inner) == B_OK) {
      if (contextLink(out, type, &inner) == B_OK)
        return B_OK;
    }
  }
  return B_NAME_NOT_FOUND;
}

status_t postAttrs(BNode *sink, BMessage *message,
                   const unsigned char *prehashed) {
  status_t status;
  unsigned char msgHash[crypto_hash_sha256_BYTES];
  if (prehashed) {
    memcpy(msgHash, prehashed, crypto_hash_sha256_BYTES);
  } else {
    JSON::RootSink rootSink(std::make_unique<JSON::Hash>(msgHash));
    JSON::fromBMessage(&rootSink, message);
  }
  BString attrString = messageCypherkey(msgHash);
  if ((status = sink->WriteAttrString("HABITAT:cypherkey", &attrString)) !=
      B_OK) {
    return status;
  }
  int64 attrNum;
  if (eitherNumber(&attrNum, message, "sequence") == B_OK) {
    if (sink->WriteAttr("HABITAT:sequence", B_INT64_TYPE, 0, &attrNum,
                        sizeof(int64)) != sizeof(int64)) {
      return B_IO_ERROR;
    }
  }
  if ((status = message->FindString("author", &attrString)) != B_OK)
    return status;
  if ((status = sink->WriteAttrString("HABITAT:author", &attrString)) != B_OK)
    return status;
  if (eitherNumber(&attrNum, message, "timestamp") == B_OK) {
    if (sink->WriteAttr("HABITAT:timestamp", B_INT64_TYPE, 0, &attrNum,
                        sizeof(int64)) != sizeof(int64)) {
      return B_IO_ERROR;
    }
  }
  if (BMessage content; message->FindMessage("content", &content) == B_OK) {
    if (BString type; content.FindString("type", &type) == B_OK) {
      sink->WriteAttrString("HABITAT:type", &type);
      if (BString context; contextLink(&context, type, &content) == B_OK)
        sink->WriteAttrString("HABITAT:context", &context);
      else
        sink->RemoveAttr("HABITAT:context");
    }
  }
  return B_OK;
}
} // namespace

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
      B_OK) {
    return status;
  }
  if ((status = message->Flatten(&sink)) != B_OK)
    return status;
  BMessage result;
  entry_ref ref;
  BEntry entry(&this->store, filename.String());
  entry.GetRef(&ref);
  result.AddRef("ref", &ref);
  result.AddString("cypherkey", messageCypherkey(msgHash));
  memcpy(this->lastHash, msgHash, crypto_hash_sha256_BYTES);
  int64 attrNum;
  if (eitherNumber(&attrNum, message, "sequence") == B_OK)
    this->lastSequence = attrNum;
  if ((status = postAttrs(&sink, message, msgHash)) != B_OK)
    return status;
  this->notifyChanges();
  if (reply != NULL)
    reply->AddMessage("result", &result);
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
  if (msg->ReturnAddress().IsValid())
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
    std::unique_ptr<JSON::NodeSink> addObject(const BString &rawname,
                                              const BString &name) override {
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
  if (message->FindDouble("sequence", &sequence) != B_OK)
    return B_NOT_ALLOWED;
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
    if (message->FindData("previous", 'NULL', &data, &numBytes) != B_OK)
      return B_NOT_ALLOWED;
    return B_OK;
  }
  return B_NOT_ALLOWED;
}

static inline status_t validateHash(BMessage *message) {
  BString hash;
  if (message->FindString("hash", &hash) != B_OK || hash != "sha256")
    return B_NOT_ALLOWED;
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
    if (message->FindString("content", &encrypted) == B_OK)
      return validateContent(encrypted);
    else
      return B_NOT_ALLOWED;
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
