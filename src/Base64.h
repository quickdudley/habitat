#ifndef BASE64_H
#define BASE64_H

#include <memory>

namespace base64 {
enum Variant {
  STANDARD,
  URL,
};

enum Error {
  INVALID_CHAR,
};

std::unique_ptr<char> encode(size_t *outlen, const unsigned char *raw,
                             size_t inlen, Variant variant);
std::unique_ptr<unsigned char> decode(size_t *outlen, const char *b64,
                                      size_t inlen);
} // namespace base64

#endif // BASE64_H
