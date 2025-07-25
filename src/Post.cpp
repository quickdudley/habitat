#include "Post.h"
#include "BJSON.h"
#include "Base64.h"
#include "Logging.h"
#include "SignJSON.h"
#include <Application.h>
#include <File.h>
#include <MessageQueue.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <StringList.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iostream>
#include <unicode/utf8.h>
#include <variant>
#include <vector>

/*
TODO: Add something to periodically recompute `context` column
*/

#define FEED_DB static_cast<SSBDatabase *>(this->Looper())->database

static inline status_t eitherNumber(int64 *result, const BMessage *source,
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

QueryBacked::QueryBacked(sqlite3_stmt *query)
    :
    query(query) {}

QueryBacked::~QueryBacked() {
  if (this->query)
    sqlite3_finalize(this->query);
}

namespace {
template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

SSBDatabase *runningDB = NULL;

enum struct TimeThreshold {
  EARLIEST,
  LATEST,
};

std::vector<std::pair<TimeThreshold, int64>>
timeBoundaries(const BMessage &specifier) {
  std::vector<std::pair<TimeThreshold, int64>> result;
  int64 value;
  for (int32 i = 0; specifier.FindInt64("earliest", i, &value) == B_OK; i++)
    result.push_back({TimeThreshold::EARLIEST, value});
  for (int32 i = 0; specifier.FindInt64("latest", i, &value) == B_OK; i++)
    result.push_back({TimeThreshold::LATEST, value});
  std::sort(
      result.begin(), result.end(),
      [](std::pair<TimeThreshold, int64> &a,
         std::pair<TimeThreshold, int64> &b) { return a.second < b.second; });
  return result;
}

status_t timestamps_clause(BString &clause,
                           std::vector<std::variant<BString, int64>> &terms,
                           const BMessage &specifier) {
  auto boundaries = timeBoundaries(specifier);
  if (boundaries.empty())
    return B_NAME_NOT_FOUND;
  std::vector<std::vector<BString>> sections;
  int state = 0;
  for (auto &[btype, boundary] : boundaries) {
    BString subclause(btype == TimeThreshold::LATEST ? "timestamp <= ?"
                                                     : "timestamp >= ?");
    switch (state) {
    case 0:
      sections.push_back(std::vector<BString>());
      sections.back().push_back(std::move(subclause));
      terms.push_back(boundary);
      if (btype == TimeThreshold::EARLIEST)
        state = 1;
      else
        state = 2;
      break;
    case 1:
      if (btype == TimeThreshold::LATEST) {
        sections.back().push_back(std::move(subclause));
        terms.push_back(boundary);
        state = 2;
      }
      break;
    case 2:
      if (btype == TimeThreshold::EARLIEST) {
        sections.push_back(std::vector<BString>());
        sections.back().push_back(std::move(subclause));
        terms.push_back(boundary);
        state = 1;
      } else {
        terms.back() = boundary;
      }
      break;
    }
  }
  clause = sections.size() > 1 ? "(" : "";
  BString d1;
  for (auto &outer : sections) {
    BString d2;
    clause << d1;
    d1 = " OR ";
    if (outer.size() > 1)
      clause << '(';
    for (auto &inner : outer) {
      clause << d2;
      d2 = " AND ";
      clause << inner;
    }
    if (outer.size() > 1)
      clause << ')';
  }
  if (sections.size() > 1)
    clause << ')';
  return B_OK;
}

class QueryHandler : public QueryBacked {
public:
  QueryHandler(sqlite3 *db, BMessenger target, const BMessage &specifier);
  void MessageReceived(BMessage *message) override;
  bool queryMatch(const BString &cypherkey, const BString &context,
                  const BMessage &msg) override;
  status_t runBulk(BMessage *reply);
  int32 limit = -1;

private:
  BMessenger target;
  BMessage specifier;
  bool started = false;
  bool ongoing = false;
  bool drips = false;

public:
  bool dregs;
  bool includeKey = false;
};

static int string_term(BString &clause,
                       std::vector<std::variant<BString, int64>> &terms,
                       const BString &attrName, const BString &columnName,
                       const BMessage &specifier) {
  std::vector<BString> values;
  int32 i = 0;
  status_t error;
  do {
    values.push_back(BString());
    error = specifier.FindString(attrName, i++, &values.back());
    if (error != B_OK)
      values.pop_back();
  } while (error != B_BAD_INDEX && error != B_NAME_NOT_FOUND);
  if (values.size() == 1) {
    clause = columnName;
    clause.Append(" = ?");
  } else if (values.size() >= 1) {
    clause = columnName;
    clause.Append(" IN(");
    for (unsigned int j = 0; j < values.size(); j++) {
      if (j > 0)
        clause.Append(", ");
      clause.Append("?");
    }
    clause.Append(")");
  }
  for (auto &value : values)
    terms.push_back(value);
  return values.size();
}

static inline sqlite3_stmt *spec2query(sqlite3 *db, const BMessage &specifier) {
  std::vector<std::variant<BString, int64>> terms;
  BString query = "SELECT cypherkey, context, body FROM messages";
  const char *separator = " WHERE ";
#define QRY_STR(attr, column)                                                  \
  {                                                                            \
    BString clause;                                                            \
    if (string_term(clause, terms, attr, column, specifier) > 0) {             \
      query.Append(separator);                                                 \
      separator = " AND ";                                                     \
      query.Append(clause);                                                    \
    }                                                                          \
  }
  QRY_STR(specifier.what == 'CPLX' ? "cypherkey" : "name", "cypherkey")
  QRY_STR("author", "author")
  QRY_STR("context", "context")
  QRY_STR("type", "type") {
    BString clause;
    if (timestamps_clause(clause, terms, specifier) == B_OK) {
      query.Append(separator);
      separator = " AND ";
      query.Append(clause);
    }
  }
#undef QRY_STR
  if (specifier.GetBool("dregs", false)) {
    query.Append(separator);
    separator = " AND ";
    query.Append("processed = 0");
  }
  sqlite3_stmt *result;
  sqlite3_prepare_v2(db, query.String(), query.Length(), &result, NULL);
  int i = 1;
  for (auto &term : terms) {
    std::visit(
        overloaded{[&](const BString &arg) {
                     sqlite3_bind_text(result, i++, arg.String(), arg.Length(),
                                       SQLITE_TRANSIENT);
                   },
                   [&](int64 arg) { sqlite3_bind_int64(result, i++, arg); }},
        term);
  }
  return result;
}

QueryHandler::QueryHandler(sqlite3 *db, BMessenger target,
                           const BMessage &specifier)
    :
    QueryBacked(spec2query(db, specifier)),
    target(target),
    specifier(specifier),
    dregs(specifier.GetBool("dregs", false)) {}

void QueryHandler::MessageReceived(BMessage *message) {
  if (!this->target.IsValid())
    goto canceled;
  switch (message->what) {
  case B_PULSE: {
    if (this->mainDone)
      break;
    bool unfinished;
    for (int i = 0;
         i < 128 && (unfinished = sqlite3_step(this->query) == SQLITE_ROW);
         i++) {
      BMessage post;
      if (post.Unflatten((const char *)sqlite3_column_blob(this->query, 2)) ==
          B_OK) {
        if (this->target.IsValid()) {
          if (this->includeKey) {
            post.AddString("cypherkey",
                           (const char *)sqlite3_column_text(this->query, 0));
          }
          this->target.SendMessage(&post);
        } else {
          goto canceled;
        }
        if (this->limit > 0 && --this->limit == 0)
          goto canceled;
      }
    }
    if (!unfinished) {
      if (this->target.IsValid()) {
        auto handle = sqlite3_db_handle(this->query);
        sqlite3_finalize(this->query);
        if (handle != FEED_DB)
          sqlite3_close(handle);
        this->query = NULL;
        this->target.SendMessage('DONE');
        this->mainDone = true;
      } else {
        goto canceled;
      }
    } else if (auto db = dynamic_cast<SSBDatabase *>(this->Looper())) {
      db->ensurePulseRunning();
    }
  } break;
  case 'CHCK':
    if (BMessage post; message->FindMessage("post", &post) == B_OK) {
      if (this->includeKey) {
        BString key;
        message->FindString("key", &key);
        post.AddString("cypherkey", key);
      }
      this->target.SendMessage(&post);
      if (this->limit > 0 && --this->limit == 0)
        goto canceled;
    }
    break;
  case 'DONE':
    if (!this->dregs && this->started && !this->drips) {
      this->drips = true;
      this->target.SendMessage('DONE');
    } else {
      this->started = true;
    }
    if (this->target.IsValid())
      break;
  case B_QUIT_REQUESTED:
  case 'STOP':
  canceled: {
    if (this->query) {
      auto handle = sqlite3_db_handle(this->query);
      sqlite3_finalize(this->query);
      if (handle != FEED_DB)
        sqlite3_close(handle);
      this->query = NULL;
    }
    BLooper *looper = this->Looper();
    looper->Lock();
    looper->RemoveHandler(this);
    looper->Unlock();
    delete this;
  } break;
  }
}

status_t QueryHandler::runBulk(BMessage *reply) {
  status_t err = B_ENTRY_NOT_FOUND;
  while (this->limit != 0 && sqlite3_step(this->query) == SQLITE_ROW) {
    BMessage post;
    status_t ierr;
    if ((ierr = post.Unflatten(
             (const char *)sqlite3_column_blob(this->query, 2))) == B_OK) {
      reply->AddMessage("result", &post);
      if (err == B_ENTRY_NOT_FOUND || err == B_OK)
        err = ierr;
      if (this->limit > 0)
        this->limit--;
    }
    if (auto *cypherkey = (const char *)sqlite3_column_text(this->query, 0))
      reply->AddString("cypherkey", cypherkey);
  }
  return err;
}

bool QueryHandler::queryMatch(const BString &cypherkey, const BString &context,
                              const BMessage &msg) {
  // TODO: Handle multiple values in specifier here too.
  if (BString specKey;
      this->specifier.FindString(this->specifier.what == 'CPLX' ? "cypherkey"
                                                                : "name",
                                 &specKey) == B_OK &&
      specKey != cypherkey) {
    return false;
  }
  const char *msgValue;
  const char *specValue;
  bool match = true;
  msg.FindString("author", &msgValue);
  for (int i = 0; this->specifier.FindString("author", i, &specValue) == B_OK;
       i++) {
    match = false;
    if (strcmp(msgValue, specValue) == 0) {
      match = true;
      break;
    }
  }
  if (!match)
    return false;
  for (int i = 0; this->specifier.FindString("context", i, &specValue) == B_OK;
       i++) {
    match = false;
    if (context == specValue) {
      match = true;
      break;
    }
  }
  if (!match)
    return false;
  BMessage content;
  // TODO: Extend this to encrypted messages that we have the key for.
  bool hasContent = msg.FindMessage("content", &content) == B_OK;
  if (hasContent)
    content.FindString("type", &msgValue);
  for (int i = 0; this->specifier.FindString("type", i, &specValue) == B_OK;
       i++) {
    match = false;
    if (strcmp(msgValue, specValue) == 0) {
      match = true;
      break;
    }
  }
  if (!match)
    return false;
  auto boundaries = timeBoundaries(this->specifier);
  if (int64 timestamp; !boundaries.empty() &&
      eitherNumber(&timestamp, &msg, "timestamp") == B_OK) {
    bool provisio = true;
    for (auto &[btype, boundary] : boundaries) {
      if (btype == TimeThreshold::EARLIEST) {
        if (timestamp < boundary)
          return false;
        provisio = true;
      } else {
        if (timestamp <= boundary)
          return true;
        provisio = false;
      }
    }
    return provisio;
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

void SSBDatabase::DispatchMessage(BMessage *message, BHandler *handler) {
  {
    auto count = this->MessageQueue()->CountMessages();
    if (count > 0) {
      BString logText("Message count: ");
      logText << count;
      writeLog('CLOG', logText);
    }
    if (this->clogged ? (count <= 512) : (count >= 8192)) {
      this->clogged = !this->clogged;
      BMessage notify('CLOG');
      notify.AddPointer("channel", this);
      notify.AddBool("clogged", this->clogged);
      BMessenger(be_app).SendMessage(&notify);
    }
  }
  BLooper::DispatchMessage(message, handler);
}

SSBDatabase::SSBDatabase(std::function<sqlite3 *()> dbOpen)
    :
    BLooper("SSB message database", 8192, 512),
    database(dbOpen()),
    dbOpen(std::move(dbOpen)) {
  if (runningDB == NULL)
    runningDB = this;
  sqlite3_prepare_v2(database,
                     "SELECT rowid, body FROM unprocessed "
                     "ORDER BY rowid LIMIT 1",
                     -1, &this->backlog, NULL);
  sqlite3_stmt *count;
  sqlite3_prepare_v2(database, "SELECT count(1) FROM unprocessed", -1, &count,
                     NULL);
  if (sqlite3_step(count) == SQLITE_ROW)
    this->backlogCount = sqlite3_column_int64(count, 0);
  sqlite3_finalize(count);
  BMessage notify('CLOG');
  notify.AddPointer("channel", this->backlog);
  notify.AddBool("clogged", true);
  BMessenger(be_app).SendMessage(&notify);
}

SSBDatabase::~SSBDatabase() {
  sqlite3_finalize(this->backlog);
  sqlite3_close_v2(this->database);
  if (runningDB == this)
    runningDB = NULL;
}

enum { kReplicatedFeed, kAReplicatedFeed, kOwnID, kPostByID };

property_info databaseProperties[] = {
    {"ReplicatedFeed",
     {B_GET_PROPERTY, B_CREATE_PROPERTY, 'USUB', 0},
     {B_DIRECT_SPECIFIER, 0},
     "A known SSB log",
     kReplicatedFeed,
     {}},
    {"ReplicatedFeed",
     {0},
     {B_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0},
     "A known SSB log",
     kAReplicatedFeed,
     {}},
    {"OwnFeed",
     {B_GET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "Our own SSB ID(s)",
     kOwnID,
     {}},
    {"Post",
     {B_GET_PROPERTY, B_SET_PROPERTY, 0},
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
  status_t error = B_MESSAGE_NOT_UNDERSTOOD;
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
          return feed;
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
    BMessage reply(error);
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    if (msg->ReturnAddress().IsValid())
      msg->SendReply(&reply);
    return NULL;
  }
}

static void freeBuffer(void *arg) { delete[] (char *)arg; }

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
          if (this->findFeed(feed, formatted) != B_OK) {
            feed = new SSBFeed(key);
            this->AddHandler(feed);
            this->feeds.insert({formatted, feed});
            feed->load();
          }
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
    case kOwnID:
      error = B_ENTRY_NOT_FOUND;
      for (int32 i = 0; i < this->CountHandlers(); i++) {
        if (OwnFeed * feed;
            (feed = dynamic_cast<OwnFeed *>(this->HandlerAt(i))) != NULL) {
          error = B_OK;
          reply.AddString("result", feed->cypherkey());
        }
      }
      break;
    case kPostByID:
      switch (msg->what) {
      case B_GET_PROPERTY: {
        QueryHandler *qh;
        bool live;
        if (BMessenger target; msg->FindMessenger("target", &target) == B_OK) {
          qh = new QueryHandler(this->dbOpen(), target, specifier);
          live = true;
          qh->includeKey = msg->GetBool("includeKey", false);
        } else {
          qh = new QueryHandler(this->database, BMessenger(), specifier);
          live = false;
        }
        qh->limit = msg->GetInt32("limit", -1);
        if (live) {
          this->Lock();
          this->AddHandler(qh);
          this->Unlock();
          reply.AddMessenger("result", BMessenger(qh));
          this->ensurePulseRunning();
          error = B_OK;
        } else {
          error = qh->runBulk(&reply);
          delete qh;
        }
      } break;
      case B_SET_PROPERTY: {
        BString cypherkey;
        BMessage data;
        if (specifier.FindString("name", &cypherkey) == B_OK &&
            msg->FindMessage("data", &data) == B_OK) {
          error = B_OK;
          if (bool value; data.FindBool("processed", &value) == B_OK) {
            sqlite3_stmt *update;
            sqlite3_prepare_v2(this->database,
                               "UPDATE messages "
                               "SET processed = ? "
                               "WHERE cypherkey = ?",
                               -1, &update, NULL);
            sqlite3_bind_int64(update, 1, value);
            sqlite3_bind_text(update, 2, cypherkey.String(), cypherkey.Length(),
                              SQLITE_STATIC);
            sqlite3_step(update);
            sqlite3_finalize(update);
          }
        }
      } break;
      }
      break;
    default:
      return BLooper::MessageReceived(msg);
    }
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    if (msg->ReturnAddress().IsValid())
      msg->SendReply(&reply);
    return;
  } else if (BString author; msg->FindString("author", &author) == B_OK) {
    sqlite3_stmt *insert;
    sqlite3_prepare_v2(this->database,
                       "INSERT INTO unprocessed (body) VALUES (?)", -1, &insert,
                       NULL);
    ssize_t flatSize = msg->FlattenedSize();
    char *buffer = new char[flatSize];
    msg->Flatten(buffer, flatSize);
    sqlite3_bind_blob64(insert, 1, buffer, flatSize, freeBuffer);
    sqlite3_step(insert);
    sqlite3_finalize(insert);
    if (++this->backlogCount > 65536) {
      BMessage notify('CLOG');
      notify.AddPointer("channel", this->backlog);
      notify.AddBool("clogged", true);
      this->initialBacklog = true;
      BMessenger(be_app).SendMessage(&notify);
    }
    this->notifyBacklog();
    this->ensurePulseRunning();
    return;
  } else if (msg->what == 'CHCK') {
    // This used to trigger garbage collection, but now I'm using it to forward
    // newly created messages to listening `QueryBacked`s
    BMessage post;
    BString msgID;
    BString context;
    if (msg->FindMessage("post", &post) == B_OK) {
      msg->FindString("key", &msgID);
      if (msg->FindString("context", &context) != B_OK)
        context = "";
      for (int i = 0; i < this->CountHandlers(); i++) {
        if (auto qh = dynamic_cast<QueryHandler *>(this->HandlerAt(i));
            qh && qh->queryMatch(msgID, context, post)) {
          BMessenger(qh).SendMessage(msg);
        }
      }
    }
  } else if (msg->what == B_PULSE && this->pulseRunning) {
    this->pulseRunning = false;
    for (int i = 0; i < this->CountHandlers(); i++) {
      if (auto qh = dynamic_cast<QueryHandler *>(this->HandlerAt(i)))
        BMessenger(qh).SendMessage(msg);
    }
    sqlite3_exec(this->database, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    if (sqlite3_step(this->backlog) == SQLITE_ROW) {
      BMessage post;
      BString author;
      if (post.Unflatten((const char *)sqlite3_column_blob(this->backlog, 1)) ==
              B_OK &&
          post.FindString("author", &author) == B_OK) {
        SSBFeed *feed;
        if (this->findFeed(feed, author) == B_OK) {
          feed->MessageReceived(&post);
        } else {
          BMessage notif(B_OBSERVER_NOTICE_CHANGE);
          notif.AddString("feed", author);
          notif.AddBool("deleted", true);
          this->SendNotices('NMSG', &notif);
        }
      }
      sqlite3_stmt *del;
      sqlite3_prepare_v2(this->database,
                         "DELETE FROM unprocessed WHERE rowid = ?", -1, &del,
                         NULL);
      sqlite3_bind_int64(del, 1, sqlite3_column_int64(this->backlog, 0));
      sqlite3_step(del);
      sqlite3_finalize(del);

      if (this->backlogCount) {
        --this->backlogCount;
        this->notifyBacklog();
      }
      this->ensurePulseRunning();
    } else {
      this->backlogCount = 0;
      this->notifyBacklog();
      if (this->initialBacklog) {
        this->initialBacklog = false;
        BMessage notify('CLOG');
        notify.AddPointer("channel", this->backlog);
        notify.AddBool("clogged", false);
        BMessenger(be_app).SendMessage(&notify);
      }
    }
    sqlite3_reset(this->backlog);
    sqlite3_exec(this->database, "END TRANSACTION;", NULL, NULL, NULL);
  } else {
    return BLooper::MessageReceived(msg);
  }
}

bool SSBDatabase::runCheck(BMessage *msg) {
  // TODO: Check that we're replicating this feed, etc
  return false;
}

status_t SSBDatabase::findFeed(SSBFeed *&result, const BString &cypherkey) {
  if (auto lookup = this->feeds.find(cypherkey); lookup != this->feeds.end()) {
    result = lookup->second;
    return B_OK;
  }
  unsigned char key[crypto_sign_PUBLICKEYBYTES];
  SSBFeed::parseAuthor(key, cypherkey);
  for (int i = 0; i < this->CountHandlers(); i++) {
    if (auto feed = dynamic_cast<SSBFeed *>(this->HandlerAt(i));
        feed && feed->matchKey(key)) {
      result = feed;
      this->feeds.insert({cypherkey, feed});
      return B_OK;
    }
  }
  return B_NAME_NOT_FOUND;
}

status_t SSBDatabase::findPost(BMessage *post, BString &cypherkey) {
  status_t error = B_ERROR;
  sqlite3_stmt *query;
  sqlite3_prepare_v2(this->database,
                     "SELECT body FROM messages WHERE cypherkey = ?", -1,
                     &query, NULL);
  sqlite3_bind_text(query, 1, cypherkey.String(), cypherkey.Length(),
                    SQLITE_TRANSIENT);
  if (sqlite3_step(query) == SQLITE_ROW)
    error = post->Unflatten((const char *)sqlite3_column_blob(query, 0));
  else
    error = B_ENTRY_NOT_FOUND;
  sqlite3_finalize(query);
  return error;
}

void SSBDatabase::notifySaved(const BString &author, int64 sequence,
                              unsigned char id[crypto_hash_sha256_BYTES]) {
  BMessage message('SNOT');
  message.AddString("author", author);
  message.AddInt64("sequence", sequence);
  message.AddData("id", B_RAW_TYPE, id, crypto_hash_sha256_BYTES, false, 1);
  BMessenger(this).SendMessage(&message);
}

void SSBDatabase::ensurePulseRunning() {
  if (!this->pulseRunning) {
    this->pulseRunning = true;
    BMessenger(this).SendMessage(B_PULSE);
  }
}

void SSBDatabase::notifyBacklog() {
  BMessage notice('BKLG');
  notice.AddUInt64("backlog", this->backlogCount);
  this->SendNotices('BKLG', &notice);
}

void SSBDatabase::loadFeeds() {
  sqlite3_stmt *query;
  sqlite3_prepare_v2(this->database, "SELECT DISTINCT author FROM feeds", -1,
                     &query, NULL);
  while (sqlite3_step(query) == SQLITE_ROW) {
    BString cypherkey((const char *)sqlite3_column_text(query, 0));
    unsigned char key[crypto_sign_PUBLICKEYBYTES];
    SSBFeed *feed;
    if (this->findFeed(feed, cypherkey) != B_OK &&
        SSBFeed::parseAuthor(key, cypherkey) == B_OK) {
      feed = new SSBFeed(key);
      this->AddHandler(feed);
      this->feeds.insert({cypherkey, feed});
      feed->load();
    }
  }
  sqlite3_finalize(query);
}

SSBFeed::SSBFeed(unsigned char key[crypto_sign_PUBLICKEYBYTES]) {
  memcpy(this->pubkey, key, crypto_sign_PUBLICKEYBYTES);
}

status_t SSBFeed::load() {
  status_t error = B_OK;
  BString key = this->cypherkey();
  {
    sqlite3_stmt *reg;
    sqlite3_prepare_v2(FEED_DB, "INSERT INTO feeds(author) VALUES(?)", -1, &reg,
                       NULL);
    sqlite3_bind_text(reg, 1, key.String(), key.Length(), SQLITE_STATIC);
    sqlite3_step(reg);
    sqlite3_finalize(reg);
  }
  sqlite3_stmt *query;
  sqlite3_prepare_v2(
      FEED_DB,
      "SELECT sequence, cypherkey FROM messages WHERE author = ?"
      " AND sequence = (SELECT max(m.sequence) FROM messages AS m"
      " WHERE m.author = messages.author)",
      -1, &query, NULL);

  sqlite3_bind_text(query, 1, key.String(), key.Length(), SQLITE_STATIC);
  if (sqlite3_step(query) == SQLITE_ROW) {
    int64 sequence = sqlite3_column_int64(query, 0);
    BString id((const char *)sqlite3_column_text(query, 1));
    BString b64;
    for (int i = 1; i < id.Length() && id[i] != '.'; i++)
      b64.Append(id[i], 1);
    std::vector<unsigned char> hash =
        base64::decode(b64.String(), b64.Length());
    if (hash.size() == crypto_hash_sha256_BYTES) {
      memcpy(this->lastHash, &hash[0], crypto_hash_sha256_BYTES);
      this->lastSequence = sequence;
    } else {
      error = B_ERROR;
    }
  }
  sqlite3_finalize(query);
  this->notifyChanges();
  return error;
}

SSBFeed::~SSBFeed() {}

bool SSBFeed::matchKey(unsigned char other[crypto_sign_PUBLICKEYBYTES]) {
  return std::memcmp(this->pubkey, other, crypto_sign_PUBLICKEYBYTES) == 0;
}

void SSBFeed::notifyChanges(BMessenger target) {
  BMessage notif(B_OBSERVER_NOTICE_CHANGE);
  notif.AddString("feed", this->cypherkey());
  notif.AddInt64("sequence", this->sequence());
  notif.AddBool("broken", this->reorder);
  notif.AddBool("forked", this->forked);
  target.SendMessage(&notif);
}

void SSBFeed::notifyChanges() {
  if (!this->broken) {
    BMessage notif(B_OBSERVER_NOTICE_CHANGE);
    notif.AddString("feed", this->cypherkey());
    notif.AddInt64("sequence", this->sequence());
    notif.AddBool("broken", this->reorder);
    notif.AddBool("forked", this->forked);
    this->reorder = false;
    this->Looper()->SendNotices('NMSG', &notif);
  }
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
        sqlite3_stmt *deleter;
        sqlite3_prepare_v2(FEED_DB, "DELETE FROM messages WHERE author = ?", -1,
                           &deleter, NULL);
        BString key = this->cypherkey();
        sqlite3_bind_text(deleter, 1, key.String(), key.Length(),
                          SQLITE_TRANSIENT);
        sqlite3_step(deleter);
        sqlite3_finalize(deleter);
        sqlite3_prepare_v2(FEED_DB, "DELETE FROM feeds WHERE author = ?", -1,
                           &deleter, NULL);
        sqlite3_bind_text(deleter, 1, key.String(), key.Length(),
                          SQLITE_TRANSIENT);
        sqlite3_step(deleter);
        sqlite3_finalize(deleter);
        reply = B_OK;
        auto looper = this->Looper();
        BMessage notif(B_OBSERVER_NOTICE_CHANGE);
        notif.AddString("feed", this->cypherkey());
        notif.AddBool("deleted", true);
        looper->SendNotices('NMSG', &notif);
        looper->Lock();
        looper->RemoveHandler(this);
        if (auto db = dynamic_cast<SSBDatabase *>(looper))
          db->feeds.erase(this->cypherkey());
        looper->Unlock();
        delete this;
        goto sendreply;
      } else {
        return BHandler::MessageReceived(msg);
      }
    }
    {
      BPropertyInfo propertyInfo(ssbFeedProperties);
      if (propertyInfo.FindMatch(msg, index, &specifier, what, property,
                                 &match) < 0) {
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
          uint16 count;
          if (specifier.FindUInt16("count", &count) == B_OK) {
            error = this->getSegment(&reply, index, count);
          } else {
            BMessage post;
            BString id;
            if ((error = this->findPost(&id, &post, index)) != B_OK)
              break;
            reply.AddMessage("result", &post);
            reply.AddString("cypherkey", id);
          }
        }
      } break;
      default:
        return BHandler::MessageReceived(msg);
      }
    }
  sendreply:
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    if (msg->ReturnAddress().IsValid())
      msg->SendReply(&reply);
  } else if (msg->what == 'SNOT') {
    int64 sequence;
    if (msg->FindInt64("sequence", &sequence) != B_OK)
      return;
    const void *id;
    ssize_t idSize;
    if (msg->FindData("id", B_RAW_TYPE, &id, &idSize) != B_OK ||
        idSize != crypto_hash_sha256_BYTES) {
      return;
    }
    this->notifyChanges();
  } else if (BString author; msg->FindString("author", &author) == B_OK &&
             author == this->cypherkey()) {
    BString lastID = this->lastSequence == 0 ? "" : this->previousLink();
    BString blank;
    status_t saveStatus;
    if ((saveStatus = post::validate(msg, this->lastSequence, lastID, false,
                                     blank)) == B_OK) {
      this->broken = false;
      this->reorder = false;
      this->forked = false;
      this->save(msg);
    } else if (saveStatus == B_LAST_BUFFER_ERROR) {
      sqlite3_stmt *rollback;
      sqlite3_prepare_v2(FEED_DB, "DELETE FROM messages WHERE author = ?", -1,
                         &rollback, NULL);
      BString key = this->cypherkey();
      sqlite3_bind_text(rollback, 1, key.String(), key.Length(), SQLITE_STATIC);
      sqlite3_step(rollback);
      sqlite3_finalize(rollback);
      this->lastSequence = 0;
      this->reorder = true;
      this->broken = false;
      this->notifyChanges();
      this->broken = true;
    } else if (saveStatus == B_MISMATCHED_VALUES) {
      this->reorder = true;
      this->notifyChanges();
      this->broken = true;
    } else {
      this->broken = true;
      // TODO: rename 'forked' because it no longer represents forking
      // but any other type of validation failure
      if (!this->forked) {
        this->forked = true;
        this->notifyChanges();
      }
      BString message("Validation failed: message on ");
      message << this->cypherkey();
      message << "; ";
      {
        JSON::RootSink rootSink(
            std::make_unique<JSON::SerializerStart>(&message, 0, false));
        JSON::fromBMessage(&rootSink, msg);
      }
      writeLog('FORK', message);
    }
  } else {
    return BHandler::MessageReceived(msg);
  }
}

status_t SSBFeed::findPost(BString *id, BMessage *post, uint64 sequence) {
  sqlite3_stmt *fetch;
  sqlite3_prepare_v2(
      FEED_DB,
      "SELECT cypherkey, body FROM messages WHERE author = ? AND sequence = ?",
      -1, &fetch, NULL);
  BString cypherkey = this->cypherkey();
  sqlite3_bind_text(fetch, 1, cypherkey.String(), cypherkey.Length(),
                    SQLITE_STATIC);
  sqlite3_bind_int64(fetch, 2, (int64)sequence);
  if (sqlite3_step(fetch) != SQLITE_ROW)
    return B_ERROR;
  if (auto *text = (const char *)sqlite3_column_text(fetch, 0))
    *id = text;
  status_t err = post->Unflatten((const char *)sqlite3_column_blob(fetch, 1));
  sqlite3_finalize(fetch);
  return err;
}

status_t SSBFeed::getSegment(BMessage *reply, uint64 sequence, uint16 count) {
  sqlite3_stmt *fetch;
  sqlite3_prepare_v2(
      FEED_DB,
      "SELECT body FROM messages WHERE author = ? AND sequence >= ? "
      "ORDER BY sequence LIMIT ?",
      -1, &fetch, NULL);
  BString cypherkey = this->cypherkey();
  sqlite3_bind_text(fetch, 1, cypherkey.String(), cypherkey.Length(),
                    SQLITE_STATIC);
  sqlite3_bind_int64(fetch, 2, (int64)sequence);
  sqlite3_bind_int64(fetch, 3, (int64)count);
  status_t err = B_OK;
  while (sqlite3_step(fetch) == SQLITE_ROW) {
    BMessage post;
    err = post.Unflatten((const char *)sqlite3_column_blob(fetch, 0));
    if (err != B_OK)
      break;
    reply->AddMessage("result", &post);
  }
  sqlite3_finalize(fetch);
  return err;
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

uint64 SSBFeed::sequence() { return this->lastSequence; }

BString SSBFeed::previousLink() {
  BString result("%");
  result.Append(base64::encode(this->lastHash, crypto_hash_sha256_BYTES,
                               base64::STANDARD));
  result.Append(".sha256");
  return result;
}

status_t SSBFeed::parseAuthor(unsigned char out[crypto_sign_PUBLICKEYBYTES],
                              const BString &in) {
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

static inline status_t checkAttr(BNode *sink, const char *attr,
                                 const BString &value) {
  BString oldValue;
  if (sink->ReadAttrString(attr, &oldValue) != B_OK || oldValue != value)
    return sink->WriteAttrString(attr, &value);
  else
    return B_OK;
}

static inline status_t checkAttr(BNode *sink, const char *attr, int64 value) {
  int64 oldValue;
  if (sink->ReadAttr(attr, B_INT64_TYPE, 0, &oldValue, sizeof(int64)) !=
          sizeof(int64) ||
      oldValue != value) {
    status_t result =
        sink->WriteAttr(attr, B_INT64_TYPE, 0, &value, sizeof(int64));
    if (result == sizeof(int64))
      return B_OK;
    else
      return result;
  } else {
    return B_OK;
  }
}
} // namespace

status_t SSBFeed::save(BMessage *message, BMessage *reply) {
  status_t status;
  unsigned char msgHash[crypto_hash_sha256_BYTES];
  {
    JSON::RootSink rootSink(std::make_unique<JSON::Hash>(msgHash));
    JSON::fromBMessage(&rootSink, message);
  }
  memcpy(this->lastHash, msgHash, crypto_hash_sha256_BYTES);
  int64 sequence;
  if (auto looper = dynamic_cast<SSBDatabase *>(this->Looper())) {
    if (!looper->pulseRunning) {
      BMessage pulse(B_PULSE);
      BMessenger(looper).SendMessage(&pulse);
      looper->pulseRunning = true;
    }
  }
  sqlite3_stmt *insert;
  sqlite3_prepare_v2(
      FEED_DB,
      "INSERT INTO messages"
      "(cypherkey, author, sequence, timestamp, type, context, body) "
      "VALUES(?, ?, ?, ?, ?, ?, ?)",
      -1, &insert, NULL);
  if (eitherNumber(&sequence, message, "sequence") == B_OK) {
    this->lastSequence = sequence;
    sqlite3_bind_int64(insert, 3, sequence);
  }
  BString cypherkey = messageCypherkey(msgHash);
  sqlite3_bind_text(insert, 1, cypherkey.String(), cypherkey.Length(),
                    SQLITE_STATIC);
  BString author = this->cypherkey();
  sqlite3_bind_text(insert, 2, author.String(), author.Length(), SQLITE_STATIC);
  int64 timestamp;
  if ((status = eitherNumber(&timestamp, message, "timestamp")) != B_OK) {
    sqlite3_finalize(insert);
    return status;
  }
  sqlite3_bind_int64(insert, 4, timestamp);
  BString context;
  if (BMessage content;
      (status = message->FindMessage("content", &content)) == B_OK) {
    BString type;
    if ((status = content.FindString("type", &type)) != B_OK) {
      sqlite3_finalize(insert);
      return status;
    }
    sqlite3_bind_text(insert, 5, type.String(), type.Length(),
                      SQLITE_TRANSIENT);
    if (contextLink(&context, type, &content) == B_OK) {
      sqlite3_bind_text(insert, 6, context.String(), context.Length(),
                        SQLITE_TRANSIENT);
    } else {
      sqlite3_bind_null(insert, 6);
    }
  } else {
    sqlite3_bind_null(insert, 5);
    sqlite3_bind_null(insert, 6);
  }
  ssize_t flatSize = message->FlattenedSize();
  char *buffer = new char[flatSize];
  message->Flatten(buffer, flatSize);
  sqlite3_bind_blob64(insert, 7, buffer, flatSize, freeBuffer);
  sqlite3_step(insert);
  sqlite3_finalize(insert);
  {
    BMessage notif('CHCK');
    notif.AddMessage("post", message);
    notif.AddString("key", cypherkey);
    notif.AddString("context", context);
    BMessenger(this->Looper()).SendMessage(&notif);
  }
  this->notifyChanges();
  if (reply != NULL) {
    BMessage result;
    result.AddString("cypherkey", messageCypherkey(msgHash));
    reply->AddMessage("result", &result);
  }
  return B_OK;
}

OwnFeed::OwnFeed(Ed25519Secret *secret)
    :
    SSBFeed(secret->pubkey) {
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

static inline status_t validateSignatureV1(BMessage *message, bool useHMac,
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

static inline status_t validateSignature(BMessage *message, bool useHMac,
                                         BString &hmacKey) {
  if (validateSignatureV1(message, useHMac, hmacKey) == B_OK)
    return B_OK;
  {
    // We occasionally get messages with fields swapped around.
    BMessage swap(message->what);
    char *propName;
    type_code typeFound;
    for (int32 i = 0;
         message->GetInfo(B_ANY_TYPE, i == 1 ? 2 : (i == 2 ? 1 : i), &propName,
                          &typeFound) == B_OK;
         i++) {
      bool fixedSize;
      int32 count;
      if (auto err = message->GetInfo(propName, &typeFound, &count, &fixedSize);
          err != B_OK) {
        return err;
      }
      const void *data;
      ssize_t dataSize;
      if (auto err = message->FindData(propName, typeFound, &data, &dataSize);
          err != B_OK) {
        return err;
      }
      swap.AddData(propName, typeFound, data, dataSize, fixedSize);
    }
    *message = swap;
  }
  return validateSignatureV1(message, useHMac, hmacKey);
}

static inline status_t validateSequence(BMessage *message, int lastSequence) {
  double sequence;
  if (message->FindDouble("sequence", &sequence) != B_OK)
    return B_BAD_VALUE;
  if (!((lastSequence <= 0 && sequence == 1) ||
        (int(sequence) - 1 == lastSequence))) {
    return B_MISMATCHED_VALUES;
  }
  return B_OK;
}

static inline status_t validatePrevious(BMessage *message, BString &lastID) {
  if (lastID != "") {
    BString previous;
    if (message->FindString("previous", &previous) != B_OK)
      return B_BAD_VALUE;
    if (previous != lastID)
      return B_LAST_BUFFER_ERROR;
    return B_OK;
  } else {
    const void *data;
    ssize_t numBytes;
    if (message->FindData("previous", 'NULL', &data, &numBytes) != B_OK)
      return B_BAD_VALUE;
    return B_OK;
  }
  return B_BAD_VALUE;
}

static inline status_t validateHash(BMessage *message) {
  BString hash;
  if (message->FindString("hash", &hash) != B_OK || hash != "sha256")
    return B_BAD_VALUE;
  return B_OK;
}

static inline status_t validateContent(BMessage *content) {
  BString type;
  if (content->FindString("type", &type) != B_OK)
    return B_BAD_VALUE;
  if (type.Length() < 3 || type.Length() > 52)
    return B_BAD_VALUE;
  return B_OK;
}

static inline status_t validateContent(BString &content) {
  if (!(content.EndsWith(".box2") || content.EndsWith(".box")))
    return B_BAD_VALUE;
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
      return B_BAD_VALUE;
  }
}

static inline status_t validateOrder(BMessage *message) {
  char *attrname;
  type_code attrtype;
  int32 index = 0;
  int32 state = 0;
  while (message->GetInfo(B_ANY_TYPE, index, &attrname, &attrtype) == B_OK) {
    switch (state) {
    case 0: {
      if (std::strcmp("previous", attrname) == 0)
        state = 1;
      else
        return B_BAD_VALUE;
    } break;
    case 1: {
      if (std::strcmp("author", attrname) == 0)
        state = 2;
      else if (std::strcmp("sequence", attrname) == 0)
        state = 3;
      else
        return B_BAD_VALUE;
    } break;
    case 2: {
      if (std::strcmp("sequence", attrname) == 0)
        state = 4;
      else
        return B_BAD_VALUE;
    } break;
    case 3: {
      if (std::strcmp("author", attrname) == 0)
        state = 4;
      else
        return B_BAD_VALUE;
    } break;
    case 4: {
      if (std::strcmp("timestamp", attrname) == 0)
        state = 5;
      else
        return B_BAD_VALUE;
    } break;
    case 5: {
      if (std::strcmp("hash", attrname) == 0)
        state = 6;
      else
        return B_BAD_VALUE;
    } break;
    case 6: {
      if (std::strcmp("content", attrname) == 0)
        state = 7;
      else
        return B_BAD_VALUE;
    } break;
    case 7: {
      if (std::strcmp("signature", attrname) == 0)
        state = 8;
      else
        return B_BAD_VALUE;
    } break;
    default: {
      return B_BAD_VALUE;
    }
    }
    index++;
  }
  return B_OK;
}

static inline status_t validateSize(BMessage *message) {
  BString serialized;
  {
    JSON::RootSink rootSink(
        std::make_unique<JSON::SerializerStart>(&serialized, 0, false));
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
    if (tally > 11192)
      return B_BAD_VALUE;
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
  CHECK(validateOrder(message));
  CHECK(validateEitherContent(message));
  CHECK(validateSize(message));
  CHECK(validateSignature(message, useHmac, hmacKey));
  CHECK(validatePrevious(message, lastID));
#undef CHECK
  return B_OK;
}
} // namespace post
