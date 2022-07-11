#ifndef SECRET_H
#define SECRET_H

#include "JSON.h"
#include <sodium.h>

struct Ed25519Secret {
  unsigned char secret[crypto_sign_SECRETKEYBYTES];
  unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
  int generate();
  void write(JSON::RootSink *target);
  BString getCypherkey();
};

namespace JSON {
class SecretNode : public NodeSink {
public:
  SecretNode(Ed25519Secret *target);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);

private:
  Ed25519Secret *target;
  bool valid;
};
} // namespace JSON

#endif
