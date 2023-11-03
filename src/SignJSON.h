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
  void addNumber(const BString &rawname, const BString &name,
                 const BString &raw, number value) override;
  void addBool(const BString &rawname, const BString &name,
               bool value) override;
  void addNull(const BString &rawname, const BString &name) override;
  void addString(const BString &rawname, const BString &name,
                 const BString &raw, const BString &value) override;
  std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                      const BString &name) override;
  std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                     const BString &name) override;

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
  void addNumber(const BString &rawname, const BString &name,
                 const BString &raw, number value) override;
  void addBool(const BString &rawname, const BString &name,
               bool value) override;
  void addNull(const BString &rawname, const BString &name) override;
  void addString(const BString &rawname, const BString &name,
                 const BString &raw, const BString &value) override;
  std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                      const BString &name) override;
  std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                     const BString &name) override;

private:
  BString body;
  unsigned char *target;
  std::unique_ptr<NodeSink> inner;
};

class VerifySignature : public NodeSink {
public:
  VerifySignature(bool *target);
  VerifySignature(bool *target, const BString &hmac);
  ~VerifySignature();
  void addNumber(const BString &rawname, const BString &name,
                 const BString &raw, number value) override;
  void addBool(const BString &rawname, const BString &name,
               bool value) override;
  void addNull(const BString &rawname, const BString &name) override;
  void addString(const BString &rawname, const BString &name,
                 const BString &raw, const BString &value) override;
  std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                      const BString &name) override;
  std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                     const BString &name) override;

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
