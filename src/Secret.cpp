#include "Secret.h"
#include "Base64.h"

int Ed25519Secret::generate() {
  return crypto_sign_keypair(this->pubkey, this->secret);
}

void Ed25519Secret::write(JSON::RootSink *target) {
  BString key;
  BString value;
  target->beginObject(key);
  key = "curve";
  value = "ed25519";
  target->addString(key, value);
  key = "public";
  BString pubkey = base64::encode(this->pubkey, crypto_sign_PUBLICKEYBYTES,
                                  base64::STANDARD);
  pubkey.Append(".ed25519");
  target->addString(key, pubkey);
  key = "private";
  value = base64::encode(this->secret, crypto_sign_SECRETKEYBYTES,
                         base64::STANDARD);
  value.Append(".ed25519");
  target->addString(key, value);
  value.SetTo("@");
  value.Append(pubkey);
  key = "id";
  target->addString(key, value);
  target->closeNode();
}

BString Ed25519Secret::getCypherkey() {
  BString result("@");
  result.Append(base64::encode(this->pubkey, crypto_sign_PUBLICKEYBYTES,
                               base64::STANDARD));
  result.Append(".ed25519");
  return result;
}

namespace JSON {

SecretNode::SecretNode(Ed25519Secret *target)
    :
    target(target) {}

void SecretNode::addString(BString &rawname, BString &name, BString &raw,
                           BString &value) {
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
  b64.RemoveLast(".ed25519");
  std::vector<unsigned char> decoded =
      base64::decode(b64.String(), b64.Length());
  memcpy(target, &decoded[0], std::min(decoded.size(), size));
}

std::unique_ptr<NodeSink> SecretNode::addObject(BString &rawname,
                                                BString &name) {
  return std::make_unique<SecretNode>(this->target);
}
} // namespace JSON