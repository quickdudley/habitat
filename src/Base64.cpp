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
    return byte - 26 + 'a';
  } else if (byte <= 61) {
    return byte - 52 + '0';
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

BString encode(const unsigned char *raw, size_t inlen, Variant variant) {
  size_t outlen = (inlen / 3 + (inlen % 3 == 0 ? 0 : 1)) * 4;
  BString result;
  unsigned char partial = 0;
  size_t r;
  for (r = 0; r < inlen; r++) {
    unsigned char byte = raw[r];
    switch (r % 3) {
    case 0:
      result.Append(encode1(byte >> 2, variant), 1);
      partial = (byte << 4) & 0x3F;
      break;
    case 1:
      result.Append(encode1((byte >> 4) | partial, variant), 1);
      partial = (byte << 2) & 0x3F;
      break;
    case 2:
      result.Append(encode1((byte >> 6) | partial, variant), 1);
      result.Append(encode1(byte & 0x3f, variant), 1);
      break;
    }
  }
  if (r % 3 != 0) {
    result.Append(encode1(partial, variant), 1);
  }
  result.Append('=', outlen - result.Length());
  return result;
}

std::vector<unsigned char> decode(const char *b64, size_t inlen) {
  int padding = 0;
  for (size_t i = inlen - 1; i >= 0; i--) {
    if (b64[i] == '=')
      padding += 1;
    if (isBase64(b64[i]))
      break;
  }
  size_t outlen = inlen * 3 / 4 - padding;
  std::vector<unsigned char> result;
  result.reserve(outlen);
  unsigned char partial = 0;
  for (size_t r = 0, c = 0; r < inlen; r++) {
    try {
      if (b64[r] == '=') {
        break;
      }
      unsigned char local = decode1(b64[r]);
      switch (c) {
      case 0:
        partial = local << 2;
        break;
      case 1:
        partial |= local >> 4;
        result.push_back(partial);
        partial = local << 4;
        break;
      case 2:
        partial |= local >> 2;
        result.push_back(partial);
        partial = local << 6;
        break;
      case 3:
        partial |= local;
        result.push_back(partial);
        break;
      }
      c = (c + 1) % 4;
    } catch (Error e) {
    }
  }
  return result;
}

std::vector<unsigned char> decode(BString &b64) {
  return decode(b64.String(), b64.Length());
}
} // namespace base64
