#include "SignJSON.h"
#include "Base64.h"
#include <cstring>
#include <utility>

namespace JSON {

SignObject::SignObject(std::unique_ptr<NodeSink> target,
                       unsigned char key[crypto_sign_SECRETKEYBYTES]) {
  this->target = std::move(target);
  this->serializer =
      std::unique_ptr<NodeSink>(new SerializerStart(&this->body));
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
  return std::unique_ptr<NodeSink>(
      new Splitter(this->object->addObject(rawname, name),
                   this->target->addObject(rawname, name)));
}

std::unique_ptr<NodeSink> SignObject::addArray(BString &rawname,
                                               BString &name) {
  return std::unique_ptr<NodeSink>(
      new Splitter(this->object->addArray(rawname, name),
                   this->target->addArray(rawname, name)));
}

Hash::Hash(unsigned char target[crypto_hash_sha256_BYTES])
    :
    target(target) {
  this->inner = std::unique_ptr<NodeSink>(new SerializerStart(&this->body));
}

Hash::~Hash() {
  this->inner.reset();
  crypto_hash_sha256(this->target, (unsigned char *)this->body.String(),
                     this->body.Length());
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
} // namespace JSON