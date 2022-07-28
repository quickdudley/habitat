#ifndef POST_H
#define POST_H

#include "Secret.h"
#include <Directory.h>
#include <Looper.h>

BString messageCypherkey(unsigned char hash[crypto_hash_sha256_BYTES]);

class SSBFeed : public BLooper {
public:
  SSBFeed(BDirectory store, unsigned char key[crypto_sign_PUBLICKEYBYTES]);

protected:
  status_t save(BMessage *message,
                unsigned char hash[crypto_hash_sha256_BYTES]);
  BDirectory store;
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
  long long lastSequence = -1;
  unsigned char lastHash[crypto_hash_sha256_BYTES];
};

class OwnFeed : public SSBFeed {
public:
  OwnFeed(BDirectory store, Ed25519Secret *secret);

private:
  unsigned char seckey[crypto_sign_SECRETKEYBYTES];
};

#endif // POST_H
