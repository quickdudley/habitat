#include "SignJSON.h"
#include "Base64.h"
#include <cstring>
#include <iostream>
#include <unicode/utf8.h>
#include <utility>
#include <vector>

namespace JSON {

SignObject::SignObject(std::unique_ptr<NodeSink> target,
                       unsigned char key[crypto_sign_SECRETKEYBYTES]) {
  this->target = std::move(target);
  this->serializer = std::make_unique<SerializerStart>(&this->body);
  BString blank;
  this->object = this->serializer->addObject(blank, blank);
  memcpy(this->key, key, crypto_sign_SECRETKEYBYTES);
}

SignObject::~SignObject() {
  this->object.reset();
  this->serializer.reset();
  unsigned char rawsig[crypto_sign_BYTES];
  unsigned long long siglen;
  crypto_sign_detached(rawsig, &siglen, (unsigned char *)this->body.String(),
                       this->body.Length(), this->key);
  BString signature = base64::encode(rawsig, siglen, base64::STANDARD);
  signature.Append(".sig.ed25519");
  BString escSig = JSON::escapeString(signature);
  BString key("signature");
  BString escKey = JSON::escapeString(key);
  this->target->addString(escKey, key, escSig, signature);
}

void SignObject::addNumber(BString &rawname, BString &name, BString &raw,
                           number value) {
  this->object->addNumber(rawname, name, raw, value);
  this->target->addNumber(rawname, name, raw, value);
}

void SignObject::addBool(BString &rawname, BString &name, bool value) {
  this->object->addBool(rawname, name, value);
  this->target->addBool(rawname, name, value);
}

void SignObject::addNull(BString &rawname, BString &name) {
  this->object->addNull(rawname, name);
  this->target->addNull(rawname, name);
}

void SignObject::addString(BString &rawname, BString &name, BString &raw,
                           BString &value) {
  this->object->addString(rawname, name, raw, value);
  this->target->addString(rawname, name, raw, value);
}

std::unique_ptr<NodeSink> SignObject::addObject(BString &rawname,
                                                BString &name) {
  return std::make_unique<Splitter>(this->object->addObject(rawname, name),
                                    this->target->addObject(rawname, name));
}

std::unique_ptr<NodeSink> SignObject::addArray(BString &rawname,
                                               BString &name) {
  return std::make_unique<Splitter>(this->object->addArray(rawname, name),
                                    this->target->addArray(rawname, name));
}

Hash::Hash(unsigned char target[crypto_hash_sha256_BYTES])
    :
    target(target) {
  this->inner = std::make_unique<SerializerStart>(&this->body);
}

Hash::~Hash() {
  this->inner.reset();
  std::vector<unsigned char> buffer;
  uint32 codepoint;
  uint32 offset = 0;
  const char *u8 = this->body.String();
  U8_NEXT_UNSAFE(u8, offset, codepoint);
  while (codepoint != 0) {
    if (codepoint >= 0x10000) {
      buffer.push_back((char)(((codepoint - 0x10000) >> 10) & 0xFF));
    }
    buffer.push_back((char)(codepoint & 0xFF));
    U8_NEXT_UNSAFE(u8, offset, codepoint);
  }
  crypto_hash_sha256(this->target, (unsigned char *)buffer.data(),
                     buffer.size());
}

void Hash::addNumber(BString &rawname, BString &name, BString &raw,
                     number value) {
  this->inner->addNumber(rawname, name, raw, value);
}

void Hash::addBool(BString &rawname, BString &name, bool value) {
  this->inner->addBool(rawname, name, value);
}

void Hash::addNull(BString &rawname, BString &name) {
  this->inner->addNull(rawname, name);
}

void Hash::addString(BString &rawname, BString &name, BString &raw,
                     BString &value) {
  this->inner->addString(rawname, name, raw, value);
}

std::unique_ptr<NodeSink> Hash::addObject(BString &rawname, BString &name) {
  return this->inner->addObject(rawname, name);
}

std::unique_ptr<NodeSink> Hash::addArray(BString &rawname, BString &name) {
  return this->inner->addArray(rawname, name);
}

class VerifyObjectSignature : public NodeSink {
public:
  VerifyObjectSignature(std::unique_ptr<NodeSink> inner, unsigned char *author,
                        unsigned char *signature);
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  std::unique_ptr<NodeSink> inner;
  unsigned char *author;
  unsigned char *signature;
};

VerifyObjectSignature::VerifyObjectSignature(std::unique_ptr<NodeSink> inner,
                                             unsigned char *author,
                                             unsigned char *signature)
    :
    author(author),
    signature(signature) {
  this->inner = std::move(inner);
}

VerifySignature::VerifySignature(bool *target)
    :
    target(target) {
  this->inner = std::make_unique<SerializerStart>(&this->body);
}

VerifySignature::VerifySignature(bool *target, BString &hmac)
    :
    VerifySignature::VerifySignature(target) {
  auto hmacBytes = base64::decode(hmac);
  if (hmacBytes.size() == crypto_auth_KEYBYTES) {
    this->useHmac = true;
    memcpy(this->hmac, hmacBytes.data(), crypto_auth_KEYBYTES);
  }
}

VerifySignature::~VerifySignature() {
  static int ix = 0;
  this->inner.reset();
  if (this->useHmac) {
    unsigned char sig1[crypto_auth_BYTES];
    crypto_auth(sig1, (unsigned char *)this->body.String(), this->body.Length(),
                this->hmac);
    *this->target =
        crypto_sign_verify_detached(this->signature, sig1, crypto_auth_BYTES,
                                    this->author) == 0;
  } else {
    *this->target = crypto_sign_verify_detached(
                        this->signature, (unsigned char *)this->body.String(),
                        this->body.Length(), this->author) == 0;
    if (!*this->target) {
      std::cerr << "Unverified signature:" << std::endl
                << this->body.String() << std::endl;
    }
  }
}

void VerifySignature::addNumber(BString &rawname, BString &name, BString &raw,
                                number value) {
  *this->target = false;
}

void VerifyObjectSignature::addNumber(BString &rawname, BString &name,
                                      BString &raw, number value) {
  this->inner->addNumber(rawname, name, raw, value);
}

void VerifySignature::addBool(BString &rawname, BString &name, bool value) {
  *this->target = false;
}

void VerifyObjectSignature::addBool(BString &rawname, BString &name,
                                    bool value) {
  this->inner->addBool(rawname, name, value);
}

void VerifySignature::addNull(BString &rawname, BString &name) {
  *this->target = false;
}

void VerifyObjectSignature::addNull(BString &rawname, BString &name) {
  this->inner->addNull(rawname, name);
}

void VerifySignature::addString(BString &rawname, BString &name, BString &raw,
                                BString &value) {
  *this->target = false;
}

void VerifyObjectSignature::addString(BString &rawname, BString &name,
                                      BString &raw, BString &value) {
  BString suffix;
  BString stuff;
  if (name == "author" && value[0] == '@' && value.EndsWith(".ed25519")) {
    value.CopyInto(stuff, 1, value.Length() - 9);
    std::vector<unsigned char> buffer = base64::decode(stuff);
    if (base64::encode(buffer, base64::STANDARD) != stuff)
      return;
    memcpy(this->author, buffer.data(), buffer.size());
    this->inner->addString(rawname, name, raw, value);
  } else if (name == "signature" && value.EndsWith(".sig.ed25519")) {
    value.CopyInto(stuff, 0, value.Length() - 12);
    std::vector<unsigned char> buffer =
        base64::decode(stuff.String(), stuff.Length());
    if (base64::encode(buffer, base64::STANDARD) != stuff)
      return;
    if (buffer.size() == crypto_sign_BYTES) {
      memcpy(this->signature, buffer.data(), crypto_sign_BYTES);
    }
  } else {
    this->inner->addString(rawname, name, raw, value);
  }
}

std::unique_ptr<NodeSink> VerifySignature::addObject(BString &rawname,
                                                     BString &name) {
  return std::make_unique<VerifyObjectSignature>(
      inner->addObject(rawname, name), this->author, this->signature);
}

std::unique_ptr<NodeSink> VerifyObjectSignature::addObject(BString &rawname,
                                                           BString &name) {
  return inner->addObject(rawname, name);
}

std::unique_ptr<NodeSink> VerifySignature::addArray(BString &rawname,
                                                    BString &name) {
  *this->target = false;
  return std::make_unique<NodeSink>();
}

std::unique_ptr<NodeSink> VerifyObjectSignature::addArray(BString &rawname,
                                                          BString &name) {
  return inner->addArray(rawname, name);
}
} // namespace JSON