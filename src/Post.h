#ifndef POST_H
#define POST_H

#include "Secret.h"
#include <Directory.h>
#include <Looper.h>
#include <PropertyInfo.h>
#include <Query.h>
#include <Volume.h>
#include <map>
#include <queue>
#include <vector>

BString messageCypherkey(unsigned char hash[crypto_hash_sha256_BYTES]);

namespace post_private_ {
struct FeedShuntEntry {
  uint64 sequence;
  entry_ref ref;
};

struct FeedBuildComparator {
  bool operator()(const FeedShuntEntry &l, const FeedShuntEntry &r);
};
} // namespace post_private_

class QueryBacked : public BHandler {
public:
  virtual bool fillQuery(BQuery *query, time_t reset) = 0;
  virtual bool queryMatch(entry_ref *entry) = 0;
};

class AntiClog : public BLooper {
public:
  AntiClog(const char *name, int32 capacity, int32 lax);
  void DispatchMessage(BMessage *message, BHandler *handler) override;

private:
  int32 capacity;
  int32 lax;
  bool clogged;
};

class SSBFeed;

extern property_info databaseProperties[];

class SSBDatabase : public AntiClog {
public:
  SSBDatabase(BDirectory store, BDirectory contacts);
  ~SSBDatabase() override;
  thread_id Run() override;
  void Quit() override;
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void MessageReceived(BMessage *msg) override;
  status_t findPost(BMessage *post, BString &cypherkey);
  status_t findFeed(SSBFeed *&result, const BString &cypherkey);
  void notifySaved(const BString &author, int64 sequence,
                   unsigned char id[crypto_hash_sha256_BYTES]);

private:
  friend class SSBFeed;
  bool runCheck(BMessage *msg);
  BDirectory store;
  BDirectory contacts;
  BQuery commonQuery;
  BMessenger writes;
  std::map<BString, SSBFeed *> feeds;
  bool pendingQueryMods = false;
  bool collectingGarbage = false;
};

class SSBFeed : public QueryBacked {
public:
  SSBFeed(BDirectory *store, BDirectory *contactsDir,
          unsigned char key[crypto_sign_PUBLICKEYBYTES]);
  ~SSBFeed();
  BString cypherkey();
  BString previousLink();
  status_t GetSupportedSuites(BMessage *data) override;
  void MessageReceived(BMessage *msg) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  status_t load(bool useCache = true);
  bool fillQuery(BQuery *query, time_t reset) override;
  bool queryMatch(entry_ref *entry) override;
  static status_t parseAuthor(unsigned char out[crypto_sign_PUBLICKEYBYTES],
                              BString &in);
  status_t findPost(BMessage *post, uint64 sequence);
  uint64 sequence();
  void notifyChanges();
  void notifyChanges(BMessenger target);

protected:
  status_t save(BMessage *message, BMessage *result = NULL);
  void cacheLatest();
  bool flushQueue();
  std::priority_queue<post_private_::FeedShuntEntry,
                      std::vector<post_private_::FeedShuntEntry>,
                      post_private_::FeedBuildComparator>
      pending;
  BDirectory *store;
  BVolume volume;
  entry_ref metastore;
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
  int64 lastSequence = 0;
  int64 savedSequence = 0;
  unsigned char lastHash[crypto_hash_sha256_BYTES];
  unsigned char savedHash[crypto_hash_sha256_BYTES];
};

class OwnFeed : public SSBFeed {
public:
  OwnFeed(BDirectory *store, BDirectory *contactsDir, Ed25519Secret *secret);
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
