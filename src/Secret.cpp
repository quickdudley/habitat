#define SECRET_CPP

#include "Secret.h"
#include "Base64.h"

int Ed25519Secret::generate() {
  return crypto_sign_keypair(this->pubkey, this->secret);
}

void Ed25519Secret::write(JSON::RootSink *target) {
  target->beginObject(BString());
  target->addString(BString("curve"), BString("ed25519"));
  size_t outlen;
  std::unique_ptr<char> b64 = base64::encode(
      &outlen, this->pubkey, crypto_sign_PUBLICKEYBYTES, base64::STANDARD);
  BString pubkey(b64.get());
  pubkey.Append(".ed25519");
  target->addString("public", pubkey);
  b64 = base64::encode(&outlen, this->secret, crypto_sign_SECRETKEYBYTES,
                       base64::STANDARD);
  BString value(b64.get());
  value.Append(".ed25519");
  target->addString("private", value);
  value.SetTo("@");
  value.Append(pubkey);
  target->addString("id", value);
  target->closeNode();
}

SecretNode::SecretNode(Ed25519Secret *target)
    :
    target(target) {}

void SecretNode::addString(BString rawname, BString name, BString raw,
                           BString value) {
  unsigned char *target;
  size_t size;
  if (name == "public") {
    target = this->target->pubkey;
    size = crypto_sign_PUBLICKEYBYTES;
    goto interesting;
  } else if (name == "private") {
    target = this->target->secret;
    size = crypto_sign_SECRETKEYBYTES;
    goto interesting;
  }
  return;
interesting:
  BString b64(value);
  value.RemoveLast(".ed25519");
  size_t outlen;
  std::unique_ptr<unsigned char> decoded =
      base64::decode(&outlen, b64.String(), b64.Length());
  memcpy(target, decoded.get(), std::min(outlen, size));
}
