#ifndef BASE64_H
#define BASE64_H

#include <String.h>
#include <vector>

namespace base64 {
enum Variant {
  STANDARD,
  URL,
};

enum Error {
  INVALID_CHAR,
};

BString encode(const unsigned char *raw, size_t inlen, Variant variant);
std::vector<unsigned char> decode(const char *b64, size_t inlen);
} // namespace base64

#endif // BASE64_H
