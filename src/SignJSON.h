#ifndef SIGNJSON_H
#define SIGNJSON_H

#include "JSON.h"
#include <sodium.h>

namespace JSON {
class SignObject : public NodeSink {
public:
  SignObject(std::unique_ptr<NodeSink> target,
             unsigned char key[crypto_sign_SECRETKEYBYTES]);
  ~SignObject();
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  std::unique_ptr<NodeSink> target;
  std::unique_ptr<NodeSink> serializer;
  std::unique_ptr<NodeSink> object;
  unsigned char key[crypto_sign_SECRETKEYBYTES];
  BString body;
};

class Hash : public NodeSink {
public:
  Hash(unsigned char target[crypto_hash_sha256_BYTES]);
  ~Hash();
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  BString body;
  unsigned char *target;
  std::unique_ptr<NodeSink> inner;
};

class VerifySignature : public NodeSink {
public:
  VerifySignature(bool *target);
  VerifySignature(bool *target, BString &hmac);
  ~VerifySignature();
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  BString body;
  bool *target;
  std::unique_ptr<NodeSink> inner;
  unsigned char author[crypto_sign_PUBLICKEYBYTES];
  unsigned char signature[crypto_sign_BYTES];
  unsigned char hmac[crypto_auth_KEYBYTES];
  bool useHmac = false;
};

} // namespace JSON

#endif // SIGNJSON_H
