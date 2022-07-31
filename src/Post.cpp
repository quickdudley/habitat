#include "Post.h"
#include "BJSON.h"
#include "Base64.h"
#include "SignJSON.h"
#include <File.h>
#include <Query.h>
#include <cstring>

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

SSBFeed::SSBFeed(BDirectory store,
                 unsigned char key[crypto_sign_PUBLICKEYBYTES])
    :
    BLooper(),
    store(store) {
  memcpy(this->pubkey, key, crypto_sign_PUBLICKEYBYTES);
}

SSBFeed::~SSBFeed() {}

status_t SSBFeed::save(BMessage *message) {
  status_t status;
  unsigned char msgHash[crypto_hash_sha256_BYTES];
  {
    JSON::RootSink rootSink(
        std::unique_ptr<JSON::NodeSink>(new JSON::Hash(msgHash)));
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
  attrString.SetTo("@");
  attrString.Append(base64::encode(this->pubkey, crypto_sign_PUBLICKEYBYTES,
                                   base64::STANDARD));
  attrString.Append(".ed25519");
  if ((status = sink.WriteAttrString("HABITAT:author", &attrString)) != B_OK)
    return status;
  int64 attrNum;
  if (eitherNumber(&attrNum, message, "sequence") == B_OK) {
    if ((status = sink.WriteAttr("HABITAT:sequence", B_INT64_TYPE, 0, &attrNum,
                                 sizeof(int64))) != B_OK)
      return status;
  }
  if (eitherNumber(&attrNum, message, "timestamp") == B_OK) {
    if ((status = sink.WriteAttr("HABITAT:timestamp", B_INT64_TYPE, 0, &attrNum,
                                 sizeof(int64))) != B_OK)
      return status;
  }
  return B_OK;
}

OwnFeed::OwnFeed(BDirectory store, Ed25519Secret *secret)
    :
    SSBFeed(store, secret->pubkey) {
  memcpy(this->seckey, secret->secret, crypto_sign_SECRETKEYBYTES);
}
