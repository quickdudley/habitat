#include "JSON.h"
#include <cctype>
#include <cmath>
#include <utility>

namespace JSON {

BString escapeString(BString src) {
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
    i++;
  }
  result << '\"';
  return result;
}

NodeSink::~NodeSink() {}

void IgnoreNode::addNumber(BString rawname, BString name, BString raw,
                           number value) {}

void IgnoreNode::addBool(BString rawname, BString name, bool value) {}

void IgnoreNode::addNull(BString rawname, BString name) {}

void IgnoreNode::addString(BString rawname, BString name, BString raw,
                           BString value) {}

std::unique_ptr<NodeSink> IgnoreNode::addObject(BString rawname, BString name) {
  return std::unique_ptr<NodeSink>(new IgnoreNode);
}

std::unique_ptr<NodeSink> IgnoreNode::addArray(BString rawname, BString name) {
  return std::unique_ptr<NodeSink>(new IgnoreNode);
}

SerializerStart::SerializerStart(BString *target) { this->target = target; }

void SerializerStart::addNumber(BString rawname, BString name, BString raw,
                                number value) {
  this->target->Append(raw);
}

void SerializerStart::addBool(BString rawname, BString name, bool value) {
  this->target->Append(value ? "true" : "false");
}

void SerializerStart::addNull(BString rawname, BString name) {
  this->target->Append("null");
}

void SerializerStart::addString(BString rawname, BString name, BString raw,
                                BString value) {
  this->target->Append(raw);
}

class ObjectSerializer : public NodeSink {
public:
  ObjectSerializer(BString *target, int indent);
  ~ObjectSerializer();
  void addNumber(BString rawname, BString name, BString raw, number value);
  void addBool(BString rawname, BString name, bool value);
  void addNull(BString rawname, BString name);
  void addString(BString rawname, BString name, BString raw, BString value);
  std::unique_ptr<NodeSink> addObject(BString rawname, BString name);
  std::unique_ptr<NodeSink> addArray(BString rawname, BString name);

private:
  void property(BString name);
  BString *target;
  int indent;
  bool nonempty;
};

class ArraySerializer : public NodeSink {
public:
  ArraySerializer(BString *target, int indent);
  ~ArraySerializer();
  void addNumber(BString rawname, BString name, BString raw, number value);
  void addBool(BString rawname, BString name, bool value);
  void addNull(BString rawname, BString name);
  void addString(BString rawname, BString name, BString raw, BString value);
  std::unique_ptr<NodeSink> addObject(BString rawname, BString name);
  std::unique_ptr<NodeSink> addArray(BString rawname, BString name);

private:
  void item();
  BString *target;
  int indent;
  bool nonempty;
};

std::unique_ptr<NodeSink> SerializerStart::addObject(BString rawname,
                                                     BString name) {
  return std::unique_ptr<NodeSink>(new ObjectSerializer(this->target, 2));
}

std::unique_ptr<NodeSink> SerializerStart::addArray(BString rawname,
                                                    BString name) {
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

void ObjectSerializer::addNumber(BString rawname, BString name, BString raw,
                                 number value) {
  this->property(rawname);
  this->target->Append(raw);
}

void ObjectSerializer::addBool(BString rawname, BString name, bool value) {
  this->property(rawname);
  this->target->Append(value ? "true" : "false");
}

void ObjectSerializer::addNull(BString rawname, BString name) {
  this->property(rawname);
  this->target->Append("null");
}

void ObjectSerializer::addString(BString rawname, BString name, BString raw,
                                 BString value) {
  this->property(rawname);
  this->target->Append(raw);
}

std::unique_ptr<NodeSink> ObjectSerializer::addObject(BString rawname,
                                                      BString name) {
  this->property(rawname);
  return std::unique_ptr<NodeSink>(
      new ObjectSerializer(this->target, this->indent + 2));
}

std::unique_ptr<NodeSink> ObjectSerializer::addArray(BString rawname,
                                                     BString name) {
  this->property(rawname);
  return std::unique_ptr<NodeSink>(
      new ArraySerializer(this->target, this->indent + 2));
}

void ObjectSerializer::property(BString rawname) {
  if (this->nonempty) {
    this->target->Append(",");
  } else {
    this->nonempty = true;
  }
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

void ArraySerializer::addNumber(BString rawname, BString name, BString raw,
                                number value) {
  this->item();
  this->target->Append(raw);
}

void ArraySerializer::addBool(BString rawname, BString name, bool value) {
  this->item();
  this->target->Append(value ? "true" : "false");
}

void ArraySerializer::addNull(BString rawname, BString name) {
  this->item();
  this->target->Append("null");
}

void ArraySerializer::addString(BString rawname, BString name, BString raw,
                                BString value) {
  this->item();
  this->target->Append(raw);
}

std::unique_ptr<NodeSink> ArraySerializer::addObject(BString rawname,
                                                     BString name) {
  this->item();
  return std::unique_ptr<NodeSink>(
      new ObjectSerializer(this->target, this->indent + 2));
}

std::unique_ptr<NodeSink> ArraySerializer::addArray(BString rawname,
                                                    BString name) {
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

void RootSink::addNumber(BString rawname, BString name, BString raw,
                         number value) {
  if (this->stack.size() > 0) {
    this->stack.back()->addNumber(rawname, name, raw, value);
  }
}

void RootSink::addNumber(BString name, number value) {
  number av = std::abs(value);
  BString raw;
  if (std::isnan(value) || std::isinf(value)) {
    raw = "null";
  } else {
    if (value < 0) {
      raw << '-';
    }
    long long n = std::max(1LL, (long long)std::floor(std::log10(av)));
    long long k = 1;
    while (av * std::pow(10.0L, (number)k) !=
           std::floor(av * std::pow(10.0L, (number)k))) {
      k++;
    }
    long long s = av * std::pow(10.0l, (number)(k - n));
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
  this->addNumber(escapeString(name), name, raw, value);
}

void RootSink::addBool(BString rawname, BString name, bool value) {
  if (this->stack.size() > 0) {
    this->stack.back()->addBool(rawname, name, value);
  }
}

void RootSink::addBool(BString name, bool value) {
  this->addBool(escapeString(name), name, value);
}

void RootSink::addNull(BString rawname, BString name) {
  if (this->stack.size() > 0) {
    this->stack.back()->addNull(rawname, name);
  }
}

void RootSink::addNull(BString name) {
  this->addNull(escapeString(name), name);
}

void RootSink::addString(BString rawname, BString name, BString raw,
                         BString value) {
  if (this->stack.size() > 0) {
    this->stack.back()->addString(rawname, name, raw, value);
  }
}

void RootSink::addString(BString name, BString value) {
  this->addString(escapeString(name), name, escapeString(value), value);
}

void RootSink::beginObject(BString rawname, BString name) {
  if (this->stack.size() > 0) {
    this->stack.push_back(this->stack.back()->addObject(rawname, name));
  }
}

void RootSink::beginObject(BString name) {
  this->beginObject(escapeString(name), name);
}

void RootSink::beginArray(BString rawname, BString name) {
  if (this->stack.size() > 0) {
    this->stack.push_back(this->stack.back()->addArray(rawname, name));
  }
}

void RootSink::beginArray(BString name) {
  this->beginArray(escapeString(name), name);
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
      this->state = 1;
      this->target->beginObject(BString());
      return B_OK;
    } else if (c == '[') {
      this->state = 2;
      this->target->beginArray(BString());
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
  } else if (this->state == 2) { // Array before entry/end
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
  }
  return B_ILLEGAL_DATA;
}

status_t Parser::charInString(char c, int cstate, int estate) {
  this->token.Append(c, 1);
  if (this->state2 == 0) {
    if (c == '\\') {
      this->state2 = 1;
      this->escape = 0;
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
      // TODO: UTF-16 to UTF-8
    }
  }
  return B_ILLEGAL_DATA;
}

} // namespace JSON
