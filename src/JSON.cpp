#include "JSON.h"
#include <cctype>
#include <cmath>
#include <utility>

namespace JSON {

BString escapeString(BString &src) {
  BString result("\"");
  for (int i = 0; i < src.Length(); i++) {
    char c = src[i];
    if (c == 0x22 || c == 0x5C) {
      result << '\\' << c;
    } else if (c == 0x08) {
      result << "\\b";
    } else if (c == 0x0C) {
      result << "\\f";
    } else if (c == 0x0A) {
      result << "\\n";
    } else if (c == 0x0D) {
      result << "\\r";
    } else if (c == 0x0B) {
      result << "\\t";
    } else if (c < 0x20) {
      BString addenum;
      addenum.SetToFormat("\\u%04x", (int)c);
      result << addenum;
    } else {
      result << c;
    }
  }
  result << '\"';
  return result;
}

BString stringifyNumber(number value) {
  BString raw;
  number av = std::abs(value);
  if (std::isnan(value) || std::isinf(value)) {
    raw = "null";
  } else {
    if (value < 0) {
      raw << '-';
    }
    int32 k = 0;
    int32 n = ((int32)std::floor(std::log10(av))) + 1;
    long long s;
    int32 wd = std::floor(std::log10(av));
    do {
      k++;
      s = std::round(av / std::pow((number)10, (number)(n - k)));
      if (k > 40) {
        *((int *)0) = 5;
      }
    } while ((number)s * std::pow((number)10, (number)(n - k)) != av);
    if (k <= n && n <= 21) {
      raw << s;
      raw.Append('0', (int32)(n - k));
    } else if (0 < n && n <= 21) {
      BString digits;
      digits << s;
      BString half;
      digits.CopyInto(half, 0, n);
      raw << half << '.';
      digits.CopyInto(half, n, k - n);
      raw << half;
    } else if (-6 < n && n <= 0) {
      raw << "0.";
      raw.Append('0', -n);
      raw << s;
    } else if (k == 1) {
      raw << s << 'e';
      long long e = n - 1;
      if (e < 0) {
        raw << '-';
      } else {
        raw << '+';
      }
      raw << std::abs(e);
    } else {
      BString digits;
      digits << s;
      raw << digits[0] << '.';
      raw << digits.String() + 1;
      raw << 'e';
      long long e = n - 1;
      if (e < 0) {
        raw << '-';
      } else {
        raw << '+';
      }
      raw << std::abs(e);
    }
  }
  return raw;
}

NodeSink::~NodeSink() {}

void NodeSink::addNumber(BString &rawname, BString &name, BString &raw,
                         number value) {}

void NodeSink::addBool(BString &rawname, BString &name, bool value) {}

void NodeSink::addNull(BString &rawname, BString &name) {}

void NodeSink::addString(BString &rawname, BString &name, BString &raw,
                         BString &value) {}

std::unique_ptr<NodeSink> NodeSink::addObject(BString &rawname, BString &name) {
  return std::unique_ptr<NodeSink>(new IgnoreNode);
}

std::unique_ptr<NodeSink> NodeSink::addArray(BString &rawname, BString &name) {
  return std::unique_ptr<NodeSink>(new IgnoreNode);
}

SerializerStart::SerializerStart(BString *target) { this->target = target; }

void SerializerStart::addNumber(BString &rawname, BString &name, BString &raw,
                                number value) {
  this->target->Append(raw);
}

void SerializerStart::addBool(BString &rawname, BString &name, bool value) {
  this->target->Append(value ? "true" : "false");
}

void SerializerStart::addNull(BString &rawname, BString &name) {
  this->target->Append("null");
}

void SerializerStart::addString(BString &rawname, BString &name, BString &raw,
                                BString &value) {
  this->target->Append(raw);
}

class ObjectSerializer : public NodeSink {
public:
  ObjectSerializer(BString *target, int indent);
  ~ObjectSerializer();
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  void property(BString &name);
  BString *target;
  int indent;
  bool nonempty;
};

class ArraySerializer : public NodeSink {
public:
  ArraySerializer(BString *target, int indent);
  ~ArraySerializer();
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  void item();
  BString *target;
  int indent;
  bool nonempty;
};

std::unique_ptr<NodeSink> SerializerStart::addObject(BString &rawname,
                                                     BString &name) {
  return std::unique_ptr<NodeSink>(new ObjectSerializer(this->target, 2));
}

std::unique_ptr<NodeSink> SerializerStart::addArray(BString &rawname,
                                                    BString &name) {
  return std::unique_ptr<NodeSink>(new ArraySerializer(this->target, 2));
}

ObjectSerializer::ObjectSerializer(BString *target, int indent) {
  this->target = target;
  this->indent = indent;
  this->nonempty = false;
  target->Append("{");
}

ObjectSerializer::~ObjectSerializer() {
  if (this->nonempty) {
    this->target->Append("\n");
    this->target->Append(' ', this->indent - 2);
  }
  this->target->Append("}");
}

void ObjectSerializer::addNumber(BString &rawname, BString &name, BString &raw,
                                 number value) {
  this->property(rawname);
  this->target->Append(raw);
}

void ObjectSerializer::addBool(BString &rawname, BString &name, bool value) {
  this->property(rawname);
  this->target->Append(value ? "true" : "false");
}

void ObjectSerializer::addNull(BString &rawname, BString &name) {
  this->property(rawname);
  this->target->Append("null");
}

void ObjectSerializer::addString(BString &rawname, BString &name, BString &raw,
                                 BString &value) {
  this->property(rawname);
  this->target->Append(raw);
}

std::unique_ptr<NodeSink> ObjectSerializer::addObject(BString &rawname,
                                                      BString &name) {
  this->property(rawname);
  return std::unique_ptr<NodeSink>(
      new ObjectSerializer(this->target, this->indent + 2));
}

std::unique_ptr<NodeSink> ObjectSerializer::addArray(BString &rawname,
                                                     BString &name) {
  this->property(rawname);
  return std::unique_ptr<NodeSink>(
      new ArraySerializer(this->target, this->indent + 2));
}

void ObjectSerializer::property(BString &rawname) {
  if (this->nonempty) {
    this->target->Append(",");
  } else {
    this->nonempty = true;
  }
  this->target->Append('\n', 1);
  this->target->Append(' ', this->indent);
  this->target->Append(rawname);
  this->target->Append(": ");
}

ArraySerializer::ArraySerializer(BString *target, int indent) {
  this->target = target;
  this->indent = indent;
  this->nonempty = false;
  target->Append("[");
}

ArraySerializer::~ArraySerializer() {
  if (this->nonempty) {
    this->target->Append("\n");
    this->target->Append(' ', this->indent - 2);
  }
  this->target->Append("]");
}

void ArraySerializer::addNumber(BString &rawname, BString &name, BString &raw,
                                number value) {
  this->item();
  this->target->Append(raw);
}

void ArraySerializer::addBool(BString &rawname, BString &name, bool value) {
  this->item();
  this->target->Append(value ? "true" : "false");
}

void ArraySerializer::addNull(BString &rawname, BString &name) {
  this->item();
  this->target->Append("null");
}

void ArraySerializer::addString(BString &rawname, BString &name, BString &raw,
                                BString &value) {
  this->item();
  this->target->Append(raw);
}

std::unique_ptr<NodeSink> ArraySerializer::addObject(BString &rawname,
                                                     BString &name) {
  this->item();
  return std::unique_ptr<NodeSink>(
      new ObjectSerializer(this->target, this->indent + 2));
}

std::unique_ptr<NodeSink> ArraySerializer::addArray(BString &rawname,
                                                    BString &name) {
  this->item();
  return std::unique_ptr<NodeSink>(
      new ArraySerializer(this->target, this->indent + 2));
}

void ArraySerializer::item() {
  if (this->nonempty) {
    this->target->Append(",");
  } else {
    this->nonempty = true;
  }
  this->target->Append(' ', this->indent);
}

RootSink::RootSink(std::unique_ptr<NodeSink> rootConsumer) {
  this->stack.push_back(std::move(rootConsumer));
}

RootSink::RootSink(NodeSink *rootConsumer) {
  this->stack.push_back(std::unique_ptr<NodeSink>(rootConsumer));
}

RootSink::~RootSink() {}

void RootSink::addNumber(BString &rawname, BString &name, BString &raw,
                         number value) {
  if (this->stack.size() > 0) {
    this->stack.back()->addNumber(rawname, name, raw, value);
  }
}

void RootSink::addNumber(BString &name, number value) {
  BString rawname = escapeString(name);
  BString raw = stringifyNumber(value);
  this->addNumber(rawname, name, raw, value);
}

void RootSink::addBool(BString &rawname, BString &name, bool value) {
  if (this->stack.size() > 0) {
    this->stack.back()->addBool(rawname, name, value);
  }
}

void RootSink::addBool(BString &name, bool value) {
  BString rawname = escapeString(name);
  this->addBool(rawname, name, value);
}

void RootSink::addNull(BString &rawname, BString &name) {
  if (this->stack.size() > 0) {
    this->stack.back()->addNull(rawname, name);
  }
}

void RootSink::addNull(BString &name) {
  BString rawname = escapeString(name);
  this->addNull(rawname, name);
}

void RootSink::addString(BString &rawname, BString &name, BString &raw,
                         BString &value) {
  if (this->stack.size() > 0) {
    this->stack.back()->addString(rawname, name, raw, value);
  }
}

void RootSink::addString(BString &name, BString &value) {
  BString rawname = escapeString(name);
  BString raw = escapeString(value);
  this->addString(rawname, name, raw, value);
}

void RootSink::beginObject(BString &rawname, BString &name) {
  if (this->stack.size() > 0) {
    this->stack.push_back(this->stack.back()->addObject(rawname, name));
  }
}

void RootSink::beginObject(BString &name) {
  BString rawname = escapeString(name);
  this->beginObject(rawname, name);
}

void RootSink::beginArray(BString &rawname, BString &name) {
  if (this->stack.size() > 0) {
    this->stack.push_back(this->stack.back()->addArray(rawname, name));
  }
}

void RootSink::beginArray(BString &name) {
  BString rawname = escapeString(name);
  this->beginArray(rawname, name);
}

void RootSink::closeNode() {
  if (this->stack.size() > 0) {
    this->stack.pop_back();
  }
}

Parser::Parser(std::unique_ptr<RootSink> target) {
  this->target = std::move(target);
}

Parser::Parser(RootSink *target) {
  this->target = std::unique_ptr<RootSink>(target);
}

Parser::Parser(std::unique_ptr<NodeSink> target) {
  this->target = std::unique_ptr<RootSink>(new RootSink(std::move(target)));
}

Parser::Parser(NodeSink *target) {
  this->target = std::unique_ptr<RootSink>(
      new RootSink(std::unique_ptr<NodeSink>(target)));
}

status_t Parser::nextChar(char c) {
  if (this->state == 0) { // Beginning of document
    if (c == '{') {
      BString blank;
      this->state = 1;
      this->stack.push_back(12);
      this->target->beginObject(blank);
      return B_OK;
    } else if (c == '[') {
      BString blank;
      this->state = 2;
      this->stack.push_back(13);
      this->target->beginArray(blank);
      return B_OK;
    }
  } else if (this->state == 1) { // Object before key/end
    if (c == '\"') {
      this->state = 3;
      this->state2 = 0;
      this->token = BString("\"");
      return B_OK;
    } else if (isspace(c)) {
      return B_OK;
    }
  } else if (this->state == 3) { // In object key
    return this->charInString(c, 3, 4);
  } else if (this->state == 4) { // Before colon in object
    if (c == ':') {
      this->state = 5;
      this->rawname = this->token;
      this->name = this->unescaped;
      this->token = BString();
      this->unescaped = BString();
      return B_OK;
    } else if (isspace(c)) {
      return B_OK;
    }
  } else if (this->state == 2 || this->state == 5) { // Before value
    if (isspace(c)) {
      return B_OK;
    } else if (c == '{') {
      this->state = 1;
      this->stack.push_back(12);
      this->target->beginObject(this->rawname, this->name);
      return B_OK;
    } else if (c == '[') {
      this->state = 2;
      this->stack.push_back(13);
      this->target->beginArray(this->rawname, this->name);
      return B_OK;
    } else if (c == '\"') {
      this->state = 6;
      this->state2 = 0;
      this->token = BString("\"");
      return B_OK;
    } else if (c == 't') {
      this->state = 7;
      this->state2 = 1;
      return B_OK;
    } else if (c == 'f') {
      this->state = 8;
      this->state2 = 1;
      return B_OK;
    } else if (c == 'n') {
      this->state = 9;
      this->state2 = 1;
      return B_OK;
    } else if (c >= '0' && c <= '9') {
      this->state = 10;
      this->token = BString();
      this->token.Append(c, 1);
      this->s = c - '0';
      this->k = 0;
      this->state2 = 0;
      return B_OK;
    } else if (c == '-') {
      this->state = 11;
      this->token = BString("-");
      this->s = 0;
      this->k = 0;
      this->state2 = 0;
      return B_OK;
    }
  } else if (this->state == 6) { // In string value
    status_t err = this->charInString(c, 6, this->stack.back());
    if (err == B_OK && this->state != 6) {
      this->target->addString(this->rawname, this->name, this->token,
                              this->unescaped);
      this->rawname = BString();
      this->name = BString();
      this->token = BString();
      this->unescaped = BString();
    }
    return err;
  } else if (this->state == 7) { // "true"
    return this->charInBool("true", true, c, 7, this->stack.back());
  } else if (this->state == 8) { // "false"
    return this->charInBool("false", false, c, 8, this->stack.back());
  } else if (this->state == 9) { // "null"
    return this->charInNull(c, 9, this->stack.back());
  } else if (this->state == 10 || this->state == 11) { // number
    return this->charInNumber(this->state == 11, c, this->state,
                              this->stack.back());
  } else if (this->state == 12) { // Object before comma or end
    if (c == ',') {
      this->state = 1;
      return B_OK;
    } else if (c == '}') {
      this->stack.pop_back();
      if (this->stack.empty()) {
        this->state = 14;
      } else {
        this->state = this->stack.back();
      }
      return B_OK;
    } else if (isspace(c)) {
      return B_OK;
    }
  } else if (this->state == 13) { // Array before comma or end
    if (c == ',') {
      this->state = 2;
      return B_OK;
    } else if (c == ']') {
      this->stack.pop_back();
      if (this->stack.empty()) {
        this->state = 14;
      } else {
        this->state = this->stack.back();
      }
      return B_OK;
    } else if (isspace(c)) {
      return B_OK;
    }
  } else if (this->state == 14) { // After end of root object
    if (isspace(c)) {
      return B_OK;
    }
  }
  return B_ILLEGAL_DATA;
}

status_t Parser::charInBool(const char *t, bool value, char c, int cstate,
                            int estate) {
  if (c == t[this->state2]) {
    this->state2++;
    if (t[this->state2] == 0) {
      this->state = estate;
      this->state2 = 0;
      this->target->addBool(this->rawname, this->name, value);
      this->rawname = BString();
      this->name = BString();
    }
    return B_OK;
  } else {
    return B_ILLEGAL_DATA;
  }
}

status_t Parser::charInNull(char c, int cstate, int estate) {
  if (c == "null"[this->state2]) {
    if (this->state2 >= 3) {
      this->state = estate;
      this->state2 = 0;
      this->target->addNull(this->rawname, this->name);
      this->rawname = BString();
      this->name = BString();
    } else {
      this->state2++;
    }
    return B_OK;
  } else {
    return B_ILLEGAL_DATA;
  }
}

template <typename T> static T raise(T base, unsigned int p) {
  T r = 1;
  while (true) {
    if (p % 2 == 1) {
      r *= base;
    }
    p /= 2;
    if (p == 0) {
      return r;
    }
    base *= base;
  }
}

status_t Parser::charInNumber(bool neg, char c, int cstate, int estate) {
  this->token.Append(c, 1);
  if (c == '0') {
    if (this->state2 <= 1) {
      this->z++;
    } else {
      this->e *= 10;
      if (this->state2 == 2) {
        this->state2 = 3;
      }
    }
    return B_OK;
  } else if (c >= '1' && c <= '9') {
    if (this->state2 <= 1) {
      this->s *= raise(10, (this->z + 1));
      this->s += c - '0';
      if (this->state2 == 1) {
        this->k += this->z + 1;
      }
      this->z = 0;
    } else {
      this->e = this->e * 10 + (c - '0');
      if (this->state2 == 2) {
        this->state2 = 3;
      }
    }
    return B_OK;
  } else if (c == '.') {
    if (this->state2 == 0) {
      this->state2 = 1;
      return B_OK;
    } else {
      return B_ILLEGAL_DATA;
    }
  } else if (c == 'e' || c == 'E') {
    if (this->state2 <= 1) {
      this->state2 = 2;
      return B_OK;
    } else {
      return B_ILLEGAL_DATA;
    }
  } else if (c == '+' && this->state2 == 2) {
    this->state2 = 3;
    return B_OK;
  } else if (c == '-' && this->state2 == 2) {
    this->state2 = 4;
    return B_OK;
  } else {
    this->state = estate;
    number p10 = (this->state2 == 4 ? this->e : -this->e) - this->k;
    this->token.Truncate(this->token.Length() - 1);
    this->target->addNumber(this->rawname, this->name, this->token,
                            s * std::pow(10, p10));
    this->rawname = "";
    this->name = "";
    this->token = "";
    this->k = 0;
    this->s = 0;
    this->e = 0;
    this->z = 0;
    this->state2 = 0;
    this->state = estate;
    return this->nextChar(c);
  }
}

status_t Parser::charInString(char c, int cstate, int estate) {
  this->token.Append(c, 1);
  if (this->state2 == 0) {
    if (c == '\\') {
      this->state2 = 1;
      this->escape = 0;
    } else if (this->highsurrogate != 0) {
      return B_ILLEGAL_DATA;
    } else if (c == '\"') {
      this->state = estate;
    } else {
      this->unescaped.Append(c, 1);
    }
    return B_OK;
  } else if (this->state2 == 1) {
    this->state2 = 0;
    switch (c) {
    case '\"':
      this->unescaped.Append(c, 1);
      break;
    case '\\':
      this->unescaped.Append(c, 1);
      break;
    case '/':
      this->unescaped.Append(c, 1);
      break;
    case 'b':
      this->unescaped.Append('\b', 1);
      break;
    case 'f':
      this->unescaped.Append('\f', 1);
      break;
    case 'n':
      this->unescaped.Append('\n', 1);
      break;
    case 't':
      this->unescaped.Append('\t', 1);
      break;
    case 'u':
      this->state2 = 2;
      break;
    default:
      return B_ILLEGAL_DATA;
    }
    if (this->highsurrogate != 0 && this->state2 != 2) {
      return B_ILLEGAL_DATA;
    }
    return B_OK;
  } else {
    int digit;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      return B_ILLEGAL_DATA;
    }
    this->escape |= digit << ((this->state2 - 2) * 4);
    if (this->state2 == 5) {
      this->state2 = 0;
      int32 codepoint;
      if (this->escape <= 0xD7FF || this->escape >= 0xE000) {
        if (this->highsurrogate != 0)
          return B_ILLEGAL_DATA;
        codepoint = this->escape;
        this->escape = 0;
      } else if (this->escape >= 0xD800 && this->escape <= 0xDBFF) {
        if (this->highsurrogate != 0)
          return B_ILLEGAL_DATA;
        this->highsurrogate = this->escape;
        this->escape = 0;
        return B_OK;
      } else {
        if (this->highsurrogate == 0)
          return B_ILLEGAL_DATA;
        codepoint = (((int32)this->highsurrogate) - 0xD800) * 0x0400 +
                    this->escape + 0x10000;
        this->highsurrogate = 0;
        this->escape = 0;
      }
      if (codepoint < 0x0080) {
        this->unescaped.Append((char)codepoint, 1);
      } else if (codepoint < 0x0800) {
        this->unescaped.Append((char)(codepoint >> 6) | 0xC0, 1);
        this->unescaped.Append((char)(codepoint & 0x3F) | 0x80, 1);
      } else if (codepoint < 0x10000) {
        this->unescaped.Append((char)(codepoint >> 12) | 0xE0, 1);
        this->unescaped.Append((char)((codepoint >> 6) & 0x3F) | 0x80, 1);
        this->unescaped.Append((char)(codepoint & 0x3F) | 0x80, 1);
      } else {
        this->unescaped.Append((char)(codepoint >> 18) | 0xF0, 1);
        this->unescaped.Append((char)((codepoint >> 12) & 0x3F) | 0x80, 1);
        this->unescaped.Append((char)((codepoint >> 6) & 0x3F) | 0x80, 1);
        this->unescaped.Append((char)(codepoint & 0x3F) | 0x80, 1);
      }
    } else {
      this->state2++;
    }
    return B_OK;
  }
  return B_ILLEGAL_DATA;
}

} // namespace JSON
