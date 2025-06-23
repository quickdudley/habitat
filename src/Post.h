#ifndef POST_H
#define POST_H

#include "Secret.h"
#include <Directory.h>
#include <Looper.h>
#include <PropertyInfo.h>
#include <Volume.h>
#include <functional>
#include <map>
#include <sqlite3.h>
#include <vector>

BString messageCypherkey(unsigned char hash[crypto_hash_sha256_BYTES]);

class QueryBacked : public BHandler {
public:
  QueryBacked(sqlite3_stmt *query);
  virtual ~QueryBacked();
  virtual bool queryMatch(const BString &cypherkey, const BString &context,
                          const BMessage &msg) = 0;

protected:
  sqlite3_stmt *query;
  bool mainDone = false;
};

class SSBFeed;

extern property_info databaseProperties[];

class SSBDatabase : public BLooper {
public:
  SSBDatabase(std::function<sqlite3*()> dbOpen);
  ~SSBDatabase() override;
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void MessageReceived(BMessage *msg) override;
  void DispatchMessage(BMessage *message, BHandler *handler) override;
  status_t findPost(BMessage *post, BString &cypherkey);
  status_t findFeed(SSBFeed *&result, const BString &cypherkey);
  void notifySaved(const BString &author, int64 sequence,
                   unsigned char id[crypto_hash_sha256_BYTES]);
  void ensurePulseRunning();
  void notifyBacklog();
  void loadFeeds();

private:
  friend class SSBFeed;
  friend class QueryHandler;
  bool runCheck(BMessage *msg);
  sqlite3 *database;
  std::function<sqlite3*()> dbOpen;
  std::map<BString, SSBFeed *> feeds;
  sqlite3_stmt *backlog;
  uint64 backlogCount;
  int checkpointCount = 0;
  bool pulseRunning = false;
  bool clogged = false;
  bool initialBacklog = true;
};

class SSBFeed : public BHandler {
public:
  SSBFeed(unsigned char key[crypto_sign_PUBLICKEYBYTES]);
  ~SSBFeed();
  BString cypherkey();
  BString previousLink();
  status_t GetSupportedSuites(BMessage *data) override;
  void MessageReceived(BMessage *msg) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  status_t load();

  static status_t parseAuthor(unsigned char out[crypto_sign_PUBLICKEYBYTES],
                              const BString &in);
  status_t findPost(BString *id, BMessage *post, uint64 sequence);
  status_t getSegment(BMessage *reply, uint64 sequence, uint16 count);
  uint64 sequence();
  bool matchKey(unsigned char other[crypto_sign_PUBLICKEYBYTES]);
  void notifyChanges();
  void notifyChanges(BMessenger target);

protected:
  status_t save(BMessage *message, BMessage *result = NULL);
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
  int64 lastSequence = 0;
  unsigned char lastHash[crypto_hash_sha256_BYTES];
  bool broken = false;
  bool forked = false;
  bool reorder = true;
};

class OwnFeed : public SSBFeed {
public:
  OwnFeed(Ed25519Secret *secret);
  status_t GetSupportedSuites(BMessage *data);
  void MessageReceived(BMessage *msg);
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property);
  status_t create(BMessage *message, BMessage *result = NULL);

private:
  unsigned char seckey[crypto_sign_SECRETKEYBYTES];
};

namespace post {
status_t validate(BMessage *message, int lastSequence, BString &lastID,
                  bool useHmac, BString &hmacKey);
}

#endif // POST_H
