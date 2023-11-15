#ifndef POST_H
#define POST_H

#include "Secret.h"
#include <Directory.h>
#include <Looper.h>
#include <PropertyInfo.h>
#include <Query.h>
#include <Volume.h>
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

class SSBFeed;

extern property_info databaseProperties[];

class SSBDatabase : public BLooper {
public:
  SSBDatabase(BDirectory store);
  ~SSBDatabase() override;
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void MessageReceived(BMessage *msg) override;
  status_t findPost(BMessage *post, BString &cypherkey);
  status_t findFeed(SSBFeed *&result, BString &cypherkey);

private:
  BDirectory store;
};

class SSBFeed : public BHandler {
public:
  SSBFeed(BDirectory store, unsigned char key[crypto_sign_PUBLICKEYBYTES]);
  ~SSBFeed();
  BString cypherkey();
  BString previousLink();
  status_t GetSupportedSuites(BMessage *data) override;
  void MessageReceived(BMessage *msg) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  status_t load();
  static status_t parseAuthor(unsigned char out[crypto_sign_PUBLICKEYBYTES],
                              BString &in);
  status_t findPost(BMessage *post, uint64 sequence);
  uint64 sequence();
  void notifyChanges();
  void notifyChanges(BMessenger target);

protected:
  status_t save(BMessage *message, BMessage *reply);
  std::priority_queue<post_private_::FeedShuntEntry,
                      std::vector<post_private_::FeedShuntEntry>,
                      post_private_::FeedBuildComparator>
      pending;
  BDirectory store;
  BVolume volume;
  BQuery query;
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
  uint64 lastSequence = 0;
  unsigned char lastHash[crypto_hash_sha256_BYTES];
};

class OwnFeed : public SSBFeed {
public:
  OwnFeed(BDirectory store, Ed25519Secret *secret);
  status_t GetSupportedSuites(BMessage *data);
  void MessageReceived(BMessage *msg);
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property);
  status_t create(BMessage *message, BMessage *reply);

private:
  unsigned char seckey[crypto_sign_SECRETKEYBYTES];
};

namespace post {
status_t validate(BMessage *message, int lastSequence, BString &lastID,
                  bool useHmac, BString &hmacKey);
}

#endif // POST_H
