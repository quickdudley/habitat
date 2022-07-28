#include "Post.h"
#include "BJSON.h"
#include "Base64.h"
#include <File.h>
#include <cstring>

BString messageCypherkey(unsigned char hash[crypto_hash_sha256_BYTES]) {
  BString result("%");
  BString body =
      base64::encode(hash, crypto_hash_sha256_BYTES, base64::STANDARD);
  result.Append(body);
  result.Append(".sha256");
  return result;
}

SSBFeed::SSBFeed(BDirectory store,
                 unsigned char key[crypto_sign_PUBLICKEYBYTES])
    :
    BLooper(),
    store(store) {
  memcpy(this->pubkey, key, crypto_sign_PUBLICKEYBYTES);
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

status_t SSBFeed::save(BMessage *message,
                       unsigned char hash[crypto_hash_sha256_BYTES]) {
  status_t status;
  BFile sink;
  BString filename =
      base64::encode(hash, crypto_hash_sha256_BYTES, base64::URL);
  if ((status = this->store.CreateFile(filename.String(), &sink, false)) !=
      B_OK)
    return status;
  BString body;
  {
    JSON::RootSink rootSink(
        std::unique_ptr<JSON::NodeSink>(new JSON::SerializerStart(&body)));
    JSON::fromBMessage(&rootSink, message);
  }
  if ((status = sink.WriteExactly(body.String(), body.Length())) != B_OK)
    return status;
  BString attrString = messageCypherkey(hash);
  if ((status = sink.WriteAttrString("HABITAT:cypherkey", &attrString)) != B_OK)
    return status;
  BMessage value;
  if ((status = message->FindMessage("value", &value)) != B_OK)
    return status;
  if (value.FindString("author", &attrString) == B_OK) {
    if ((status = sink.WriteAttrString("HABITAT:author", &attrString)) != B_OK)
      return status;
  }
  int64 attrNum;
  if (eitherNumber(&attrNum, &value, "sequence") == B_OK) {
    if ((status = sink.WriteAttr("HABITAT:sequence", B_INT64_TYPE, 0, &attrNum,
                                 sizeof(int64))) != B_OK)
      return status;
  }
  if (eitherNumber(&attrNum, &value, "timestamp") == B_OK) {
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
