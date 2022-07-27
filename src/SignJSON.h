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
} // namespace JSON

#endif // SIGNJSON_H
