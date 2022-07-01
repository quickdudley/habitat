#define BASE64_CPP
#include "Base64.h"

namespace base64 {

static inline bool isBase64(char point) {
  return point >= 'A' && point <= 'Z' || point >= 'a' && point <= 'z' ||
         point >= '0' && point <= '9' || point == '+' || point == '-' ||
         point == '/' || point == '_';
}

static inline unsigned char decode1(char point) {
  if (point >= 'A' && point <= 'Z') {
    return point - 'A';
  } else if (point >= 'a' && point <= 'z') {
    return point - 'a' + 26;
  } else if (point >= '0' && point <= '9') {
    return point - '0' + 52;
  } else if (point == '+' || point == '-') {
    return 62;
  } else if (point == '/' || point == '_') {
    return 63;
  } else {
    throw INVALID_CHAR;
  }
}

static inline char encode1(unsigned char byte, Variant variant) {
  if (byte <= 25) {
    return byte + 'A';
  } else if (byte <= 51) {
    return byte + 'a';
  } else if (byte <= 61) {
    return byte + '0';
  } else if (byte == 62) {
    if (variant == STANDARD) {
      return '+';
    } else {
      return '-';
    }
  } else {
    if (variant == STANDARD) {
      return '/';
    } else {
      return '_';
    }
  }
}

std::unique_ptr<char> encode(size_t *outlen, const unsigned char *raw,
                             size_t inlen, Variant variant) {
  *outlen = (inlen / 3 + (inlen % 3 == 0 ? 0 : 1)) * 4;
  std::unique_ptr<char> result = std::unique_ptr<char>(new char[*outlen]);
  size_t w = 0;
  unsigned char partial = 0;
  for (size_t r = 0; r < inlen; r++) {
    unsigned char byte = raw[r];
    switch (r % 3) {
    case 0:
      result.get()[w] = encode1(byte >> 2, variant);
      w++;
      partial = (byte << 4) & 0x7F;
      break;
    case 1:
      result.get()[w] = encode1((byte >> 4) | partial, variant);
      w++;
      partial = (byte << 2) & 0x7F;
      break;
    case 2:
      result.get()[w] = encode1((byte >> 6) | partial, variant);
      w++;
      result.get()[w] = encode1(byte & 0x7F, variant);
      w++;
      break;
    }
  }
  for (; w < *outlen; w++) {
    result.get()[w] = '=';
  }
  return result;
}

std::unique_ptr<unsigned char> decode(size_t *outlen, const char *b64,
                                      size_t inlen) {
  int padding = 0;
  for (size_t i = inlen - 1; i >= 0; i--) {
    if (b64[i] == '=')
      padding += 1;
    if (isBase64(b64[i]))
      break;
  }
  *outlen = inlen * 3 / 4 - padding;
  size_t w = 0, trunc = 0;
  std::unique_ptr<unsigned char> result =
      std::unique_ptr<unsigned char>(new unsigned char[*outlen]);
  for (size_t r = 0; r < inlen && w < *outlen; r++) {
    try {
      if (b64[r] == '=')
        break;
      unsigned char point = decode1(b64[r]);
      switch ((r - trunc) % 4) {
      case 0:
        result.get()[w] |= point << 2;
        break;
      case 1:
        result.get()[w] |= point >> 4;
        w++;
        if (w < *outlen)
          result.get()[w] = point << 4;
        break;
      case 2:
        result.get()[w] |= point >> 2;
        w++;
        if (w < *outlen)
          result.get()[w] = point << 6;
        break;
      case 3:
        result.get()[w] |= point;
        w++;
        break;
      }
    } catch (Error &err) {
      trunc++;
    }
  }
  *outlen = (inlen - trunc) * 3 / 4 - padding;
  return result;
}
} // namespace base64
