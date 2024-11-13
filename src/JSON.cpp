#include "JSON.h"
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>

namespace JSON {

BString escapeString(const BString &src) {
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
    } else if (c == 0x09) {
      result << "\\t";
    } else if (c >= 0 && c < 0x20) {
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

template <typename T> static T raise(T base, unsigned int p) {
  T r = 1;
  while (true) {
    if (p % 2 == 1)
      r *= base;
    p /= 2;
    if (p == 0)
      return r;
    base *= base;
  }
}

BString stringifyNumber(number value) {
  if (value == 0)
    return BString("0");
  BString raw;
  number av = std::abs(value);
  if (std::isnan(value) || std::isinf(value)) {
    raw = "null";
  } else {
    if (value < 0)
      raw << '-';
    int32 k = 0;
    int32 n = ((int32)std::floor(std::log10(av))) + 1;
    long long s;
    long double f10;
    do {
      k++;
      if (n > k) {
        f10 = raise((long double)10, n - k);
        long double lowered = av / f10;
        s = std::floor(lowered);
        if (lowered - s > 0.5)
          s++;
      } else {
        f10 = raise((long double)10, k - n);
        long double raised = av * f10;
        s = std::floor(raised);
        if (raised - s > 0.5)
          s++;
      }
    } while ((number)(n > k ? s * f10 : s / f10) != av);
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
      if (e < 0)
        raw << '-';
      else
        raw << '+';
      raw << std::abs(e);
    } else {
      BString digits;
      digits << s;
      raw << digits[0] << '.';
      raw << digits.String() + 1;
      raw << 'e';
      long long e = n - 1;
      if (e < 0)
        raw << '-';
      else
        raw << '+';
      raw << std::abs(e);
    }
  }
  return raw;
}

NodeSink::~NodeSink() {}

void NodeSink::addNumber(const BString &rawname, const BString &name,
                         const BString &raw, number value) {}

void NodeSink::addBool(const BString &rawname, const BString &name,
                       bool value) {}

void NodeSink::addNull(const BString &rawname, const BString &name) {}

void NodeSink::addString(const BString &rawname, const BString &name,
                         const BString &raw, const BString &value) {}

std::unique_ptr<NodeSink> NodeSink::addObject(const BString &rawname,
                                              const BString &name) {
  return std::make_unique<IgnoreNode>();
}

std::unique_ptr<NodeSink> NodeSink::addArray(const BString &rawname,
                                             const BString &name) {
  return std::make_unique<IgnoreNode>();
}

SerializerStart::SerializerStart(BString *target) { this->target = target; }

void SerializerStart::addNumber(const BString &rawname, const BString &name,
                                const BString &raw, number value) {
  this->target->Append(raw);
}

void SerializerStart::addBool(const BString &rawname, const BString &name,
                              bool value) {
  this->target->Append(value ? "true" : "false");
}

void SerializerStart::addNull(const BString &rawname, const BString &name) {
  this->target->Append("null");
}

void SerializerStart::addString(const BString &rawname, const BString &name,
                                const BString &raw, const BString &value) {
  this->target->Append(raw);
}

class ObjectSerializer : public NodeSink {
public:
  ObjectSerializer(BString *target, int indent);
  ~ObjectSerializer();
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
  void property(const BString &name);
  BString *target;
  int indent;
  bool nonempty;
};

class ArraySerializer : public NodeSink {
public:
  ArraySerializer(BString *target, int indent);
  ~ArraySerializer();
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
  void item();
  BString *target;
  int indent;
  bool nonempty;
};

std::unique_ptr<NodeSink> SerializerStart::addObject(const BString &rawname,
                                                     const BString &name) {
  return std::make_unique<ObjectSerializer>(this->target, 2);
}

std::unique_ptr<NodeSink> SerializerStart::addArray(const BString &rawname,
                                                    const BString &name) {
  return std::make_unique<ArraySerializer>(this->target, 2);
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

void ObjectSerializer::addNumber(const BString &rawname, const BString &name,
                                 const BString &raw, number value) {
  this->property(rawname);
  this->target->Append(raw);
}

void ObjectSerializer::addBool(const BString &rawname, const BString &name,
                               bool value) {
  this->property(rawname);
  this->target->Append(value ? "true" : "false");
}

void ObjectSerializer::addNull(const BString &rawname, const BString &name) {
  this->property(rawname);
  this->target->Append("null");
}

void ObjectSerializer::addString(const BString &rawname, const BString &name,
                                 const BString &raw, const BString &value) {
  this->property(rawname);
  this->target->Append(raw);
}

std::unique_ptr<NodeSink> ObjectSerializer::addObject(const BString &rawname,
                                                      const BString &name) {
  this->property(rawname);
  return std::make_unique<ObjectSerializer>(this->target, this->indent + 2);
}

std::unique_ptr<NodeSink> ObjectSerializer::addArray(const BString &rawname,
                                                     const BString &name) {
  this->property(rawname);
  return std::make_unique<ArraySerializer>(this->target, this->indent + 2);
}

void ObjectSerializer::property(const BString &rawname) {
  if (this->nonempty)
    this->target->Append(",");
  else
    this->nonempty = true;
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

void ArraySerializer::addNumber(const BString &rawname, const BString &name,
                                const BString &raw, number value) {
  this->item();
  this->target->Append(raw);
}

void ArraySerializer::addBool(const BString &rawname, const BString &name,
                              bool value) {
  this->item();
  this->target->Append(value ? "true" : "false");
}

void ArraySerializer::addNull(const BString &rawname, const BString &name) {
  this->item();
  this->target->Append("null");
}

void ArraySerializer::addString(const BString &rawname, const BString &name,
                                const BString &raw, const BString &value) {
  this->item();
  this->target->Append(raw);
}

std::unique_ptr<NodeSink> ArraySerializer::addObject(const BString &rawname,
                                                     const BString &name) {
  this->item();
  return std::make_unique<ObjectSerializer>(this->target, this->indent + 2);
}

std::unique_ptr<NodeSink> ArraySerializer::addArray(const BString &rawname,
                                                    const BString &name) {
  this->item();
  return std::make_unique<ArraySerializer>(this->target, this->indent + 2);
}

void ArraySerializer::item() {
  if (this->nonempty)
    this->target->Append(",");
  else
    this->nonempty = true;
  this->target->Append('\n', 1);
  this->target->Append(' ', this->indent);
}

Splitter::Splitter(std::unique_ptr<NodeSink> a, std::unique_ptr<NodeSink> b) {
  this->a = std::move(a);
  this->b = std::move(b);
}

void Splitter::addNumber(const BString &rawname, const BString &name,
                         const BString &raw, number value) {
  this->a->addNumber(rawname, name, raw, value);
  this->b->addNumber(rawname, name, raw, value);
}

void Splitter::addBool(const BString &rawname, const BString &name,
                       bool value) {
  this->a->addBool(rawname, name, value);
  this->b->addBool(rawname, name, value);
}

void Splitter::addNull(const BString &rawname, const BString &name) {
  this->a->addNull(rawname, name);
  this->b->addNull(rawname, name);
}

void Splitter::addString(const BString &rawname, const BString &name,
                         const BString &raw, const BString &value) {
  this->a->addString(rawname, name, raw, value);
  this->b->addString(rawname, name, raw, value);
}

std::unique_ptr<NodeSink> Splitter::addObject(const BString &rawname,
                                              const BString &name) {
  return std::make_unique<Splitter>(this->a->addObject(rawname, name),
                                    this->b->addObject(rawname, name));
}

std::unique_ptr<NodeSink> Splitter::addArray(const BString &rawname,
                                             const BString &name) {
  return std::make_unique<Splitter>(this->a->addArray(rawname, name),
                                    this->b->addArray(rawname, name));
}

RootSink::RootSink(std::unique_ptr<NodeSink> rootConsumer) {
  this->stack.push_back(std::move(rootConsumer));
}

RootSink::RootSink(NodeSink *rootConsumer) {
  this->stack.push_back(std::unique_ptr<NodeSink>(rootConsumer));
}

RootSink::~RootSink() {
  while (!this->stack.empty())
    this->stack.pop_back();
}

void RootSink::addNumber(const BString &rawname, const BString &name,
                         const BString &raw, number value) {
  if (this->stack.size() > 0)
    this->stack.back()->addNumber(rawname, name, raw, value);
}

void RootSink::addNumber(const BString &name, number value) {
  BString rawname = escapeString(name);
  BString raw = stringifyNumber(value);
  this->addNumber(rawname, name, raw, value);
}

void RootSink::addNumber(const char *name, number value) {
  BString strName(name);
  this->addNumber(strName, value);
}

void RootSink::addBool(const BString &rawname, const BString &name,
                       bool value) {
  if (this->stack.size() > 0)
    this->stack.back()->addBool(rawname, name, value);
}

void RootSink::addBool(const BString &name, bool value) {
  BString rawname = escapeString(name);
  this->addBool(rawname, name, value);
}

void RootSink::addBool(const char *name, bool value) {
  BString strName(name);
  this->addBool(strName, value);
}

void RootSink::addNull(const BString &rawname, const BString &name) {
  if (this->stack.size() > 0)
    this->stack.back()->addNull(rawname, name);
}

void RootSink::addNull(const BString &name) {
  BString rawname = escapeString(name);
  this->addNull(rawname, name);
}

void RootSink::addNull(const char *name) {
  BString strName(name);
  this->addNull(strName);
}

void RootSink::addString(const BString &rawname, const BString &name,
                         const BString &raw, const BString &value) {
  if (this->stack.size() > 0)
    this->stack.back()->addString(rawname, name, raw, value);
}

void RootSink::addString(const BString &name, const BString &value) {
  BString rawname = escapeString(name);
  BString raw = escapeString(value);
  this->addString(rawname, name, raw, value);
}

void RootSink::beginObject(const BString &rawname, const BString &name) {
  if (this->stack.size() > 0)
    this->stack.push_back(this->stack.back()->addObject(rawname, name));
}

void RootSink::beginObject(const BString &name) {
  BString rawname = escapeString(name);
  this->beginObject(rawname, name);
}

void RootSink::beginArray(const BString &rawname, const BString &name) {
  if (this->stack.size() > 0)
    this->stack.push_back(this->stack.back()->addArray(rawname, name));
}

void RootSink::beginArray(const BString &name) {
  BString rawname = escapeString(name);
  this->beginArray(rawname, name);
}

void RootSink::closeNode() {
  if (this->stack.size() > 0)
    this->stack.pop_back();
}

status_t parse(std::unique_ptr<NodeSink> target, BDataIO *input) {
  Parser parser(std::move(target));
  return parse(&parser, input);
}

status_t parse(Parser *target, BDataIO *input) {
  char buffer[1024];
  ssize_t readBytes;
  while ((readBytes = input->Read(buffer, sizeof(buffer))) > 0) {
    for (int i = 0; i < readBytes; i++) {
      status_t parseResult = target->nextChar(buffer[i]);
      if (parseResult != B_OK)
        return parseResult;
    }
  }
  return B_OK;
}

status_t parse(NodeSink *target, BDataIO *input) {
  return parse(std::unique_ptr<NodeSink>(target), input);
}

status_t parse(std::unique_ptr<NodeSink> target, BDataIO *input, size_t bytes) {
  JSON::Parser parser(std::move(target));
  return parse(&parser, input, bytes);
}

status_t parse(Parser *target, BDataIO *input, size_t bytes) {
  //  static BFile dump("jsondump", B_WRITE_ONLY | B_CREATE_FILE);
  char buffer[1024];
  status_t result;
  ssize_t remaining = bytes;
  while (remaining > 0) {
    ssize_t count = input->Read(
        buffer, remaining > sizeof(buffer) ? sizeof(buffer) : remaining);
    remaining -= count;
    //    dump.WriteExactly(buffer, count);
    if (count <= 0) {
      std::cout << std::endl;
      return B_PARTIAL_READ;
    }
    for (int i = 0; i < count; i++) {
      if ((result = target->nextChar(buffer[i])) != B_OK) {
        while (remaining > 0 && count > 0) {
          count = input->Read(
              buffer, remaining > sizeof(buffer) ? sizeof(buffer) : remaining);
          remaining -= count;
        }
        std::cout << std::endl;
        return result;
      }
    }
  }
  //  dump.WriteExactly("\n\n", 2);
  //  dump.Flush();
  return B_OK;
}

status_t parse(NodeSink *target, BDataIO *input, size_t bytes) {
  return parse(std::unique_ptr<NodeSink>(target), input, bytes);
}

status_t parse(std::unique_ptr<NodeSink> target, const char *input) {
  Parser parser(std::move(target));
  return parse(&parser, input);
}

status_t parse(Parser *target, const char *input) {
  for (int i = 0; input[i] != 0; i++) {
    status_t parseResult = target->nextChar(input[i]);
    if (parseResult != B_OK)
      return parseResult;
  }
  return B_OK;
}

status_t parse(NodeSink *target, const char *input) {
  return parse(std::unique_ptr<NodeSink>(target), input);
}

status_t parse(std::unique_ptr<NodeSink> target, const char *input,
               size_t bytes) {
  Parser parser(std::move(target));
  return parse(&parser, input, bytes);
}

status_t parse(Parser *target, const char *input, size_t bytes) {
  for (size_t i = 0; i < bytes; i++) {
    status_t result = target->nextChar(input[i]);
    if (result != B_OK)
      return result;
  }
  return B_OK;
}

status_t parse(NodeSink *target, const char *input, size_t bytes) {
  return parse(std::unique_ptr<NodeSink>(target), input, bytes);
}

status_t parse(std::unique_ptr<NodeSink> target, const BString &input) {
  Parser parser(std::move(target));
  return parse(&parser, input);
}

status_t parse(Parser *target, const BString &input) {
  for (int i = 0; i < input.Length(); i++) {
    status_t parseResult = target->nextChar(input[i]);
    if (parseResult != B_OK)
      return parseResult;
  }
  return B_OK;
}

status_t parse(NodeSink *target, const BString &input) {
  return parse(std::unique_ptr<NodeSink>(target), input);
}

Parser::Parser(std::unique_ptr<RootSink> target, bool lax)
    :
    lax(lax) {
  this->target = std::move(target);
  this->stack.push_back(14);
}

Parser::Parser(RootSink *target, bool lax)
    :
    lax(lax) {
  this->target = std::unique_ptr<RootSink>(target);
  this->stack.push_back(14);
}

Parser::Parser(std::unique_ptr<NodeSink> target, bool lax)
    :
    lax(lax) {
  this->target = std::make_unique<RootSink>(std::move(target));
  this->stack.push_back(14);
}

Parser::Parser(NodeSink *target, bool lax)
    :
    lax(lax) {
  this->target = std::make_unique<RootSink>(std::unique_ptr<NodeSink>(target));
  this->stack.push_back(14);
}

status_t Parser::nextChar(char c) {
  if (this->state == 0 && !this->lax) { // Beginning of document
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
    } else if (c == '}') {
      this->stack.pop_back();
      if (this->stack.empty())
        this->state = 14;
      else
        this->state = this->stack.back();
      this->target->closeNode();
      return B_OK;
    } else if (isspace(c)) {
      return B_OK;
    }
  } else if (this->state == 2 && c == ']') {
    this->stack.pop_back();
    if (this->stack.empty())
      this->state = 14;
    else
      this->state = this->stack.back();
    this->target->closeNode();
    return B_OK;
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
  } else if (this->state == 2 || this->state == 5 ||
             (this->lax && this->state == 0)) { // Before value
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
      this->state2 = 0;
      return B_OK;
    } else if (c == '-') {
      this->state = 11;
      this->token = BString("-");
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
      if (this->stack.empty())
        this->state = 14;
      else
        this->state = this->stack.back();
      this->target->closeNode();
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
      if (this->stack.empty())
        this->state = 14;
      else
        this->state = this->stack.back();
      this->target->closeNode();
      return B_OK;
    } else if (isspace(c)) {
      return B_OK;
    }
  } else if (this->state == 14) { // After end of root object
    if (isspace(c))
      return B_OK;
  }
  return B_ILLEGAL_DATA;
}

void Parser::setPropName(const BString &name) {
  this->name = name;
  this->rawname = escapeString(name);
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

status_t Parser::charInNumber(bool neg, char c, int cstate, int estate) {
  this->token.Append(c, 1);
  if (c == '0') {
    if (this->state2 != 0 && this->state2 != 1) {
      if (this->state2 == 2)
        this->state2 = 3;
    }
    return B_OK;
  } else if (c >= '1' && c <= '9') {
    if (this->state2 > 1) {
      if (this->state2 == 2)
        this->state2 = 3;
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
    this->token.Truncate(this->token.Length() - 1);
    this->target->addNumber(this->rawname, this->name, this->token,
                            std::stod(this->token.String()));
    this->rawname = "";
    this->name = "";
    this->token = "";
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
      this->unescaped.Append((char)0x08, 1);
      break;
    case 'f':
      this->unescaped.Append((char)0x0C, 1);
      break;
    case 'n':
      this->unescaped.Append((char)0x0A, 1);
      break;
    case 'r':
      this->unescaped.Append((char)0x0D, 1);
      break;
    case 't':
      this->unescaped.Append((char)0x09, 1);
      break;
    case 'u':
      this->state2 = 2;
      break;
    default:
      return B_ILLEGAL_DATA;
    }
    if (this->highsurrogate != 0 && this->state2 != 2)
      return B_ILLEGAL_DATA;
    return B_OK;
  } else {
    int digit;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (c >= 'a' && c <= 'f')
      digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      digit = c - 'A' + 10;
    else
      return B_ILLEGAL_DATA;
    this->escape |= digit << ((5 - this->state2) * 4);
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
