#include "Post.h"
#include "BJSON.h"
#include "Base64.h"
#include "SignJSON.h"
#include <File.h>
#include <Path.h>
#include <Query.h>
#include <Volume.h>
#include <cstring>
#include <fs_info.h>

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
  BVolume volume;
  BQuery query;
  {
    BEntry entry(&store, ".");
    BPath path;
    status_t status = entry.GetPath(&path);
    dev_t device = dev_for_path(path.Path());
    volume.SetTo(device);
  }
  query.SetVolume(&volume);
  query.PushAttr("HABITAT:author");
  BString attrValue = this->cypherkey();
  query.PushString(attrValue.String());
  query.PushOp(B_EQ);
  if (query.Fetch() == B_OK) {
    entry_ref ref;
    while (query.GetNextRef(&ref) == B_OK) {
      BEntry entry(&ref);
      BNode node(&entry);
      int64 sequence;
      node.ReadAttr("HABITAT:sequence", B_INT64_TYPE, 0, &sequence,
                    sizeof(int64));
      if (sequence > this->lastSequence) {
        this->lastSequence = sequence;
        BString cypherkey;
        node.ReadAttrString("HABITAT:cypherkey", &cypherkey);
        BString b64;
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

BString SSBFeed::cypherkey() {
  BString result("@");
  result.Append(base64::encode(this->pubkey, crypto_sign_PUBLICKEYBYTES,
                               base64::STANDARD));
  result.Append(".ed25519");
  return result;
}

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
  attrString = this->cypherkey();
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