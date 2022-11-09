#ifndef POST_H
#define POST_H

#include "Secret.h"
#include <Directory.h>
#include <Looper.h>
#include <Query.h>
#include <Volume.h>

BString messageCypherkey(unsigned char hash[crypto_hash_sha256_BYTES]);

class SSBFeed : public BLooper {
public:
  SSBFeed(BDirectory store, unsigned char key[crypto_sign_PUBLICKEYBYTES]);
  ~SSBFeed();
  thread_id Run();
  BString cypherkey();
  BString previousLink();
  status_t GetSupportedSuites(BMessage *data);
  void MessageReceived(BMessage *msg);
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property);

protected:
  status_t save(BMessage *message, BMessage *reply);
  BDirectory store;
  BVolume volume;
  BQuery updateQuery;
  BMessenger updateMessenger;
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
  int64 lastSequence = -1;
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

#endif // POST_H
