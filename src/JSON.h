#ifndef JSON_H
#define JSON_H

#include <DataIO.h>
#include <String.h>
#include <memory>
#include <vector>

namespace JSON {

typedef double number;

BString escapeString(BString &src);
BString stringifyNumber(number value);

class NodeSink {
public:
  virtual ~NodeSink();
  virtual void addNumber(BString &rawname, BString &name, BString &raw,
                         number value);
  virtual void addBool(BString &rawname, BString &name, bool value);
  virtual void addNull(BString &rawname, BString &name);
  virtual void addString(BString &rawname, BString &name, BString &raw,
                         BString &value);
  virtual std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  virtual std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);
};

typedef NodeSink IgnoreNode;

class SerializerStart : public NodeSink {
public:
  SerializerStart(BString *target);
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  BString *target;
};

class Splitter : public NodeSink {
public:
  Splitter(std::unique_ptr<NodeSink> a, std::unique_ptr<NodeSink> b);
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  std::unique_ptr<NodeSink> a;
  std::unique_ptr<NodeSink> b;
};

class RootSink {
public:
  RootSink(std::unique_ptr<NodeSink> rootConsumer);
  RootSink(NodeSink *rootConsumer);
  virtual ~RootSink();
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addNumber(BString &name, number value);
  void addNumber(const char *name, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addBool(BString &name, bool value);
  void addBool(const char *name, bool value);
  void addNull(BString &rawname, BString &name);
  void addNull(BString &name);
  void addNull(const char *name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  void addString(BString &name, BString &value);
  void beginObject(BString &rawname, BString &name);
  void beginObject(BString &name);
  void beginArray(BString &rawname, BString &name);
  void beginArray(BString &name);
  void closeNode();

private:
  std::vector<std::unique_ptr<NodeSink>> stack;
};

status_t parse(std::unique_ptr<NodeSink> target, BDataIO *input);
status_t parse(NodeSink *target, BDataIO *input);
status_t parse(std::unique_ptr<NodeSink> target, BDataIO *input, size_t bytes);
status_t parse(NodeSink *target, BDataIO *input, size_t bytes);
status_t parse(std::unique_ptr<NodeSink> target, const char *input);
status_t parse(NodeSink *target, const char *input);
status_t parse(std::unique_ptr<NodeSink> target, const char *input,
               size_t bytes);
status_t parse(NodeSink *target, const char *input, size_t bytes);
status_t parse(std::unique_ptr<NodeSink> target, BString &input);
status_t parse(NodeSink *target, BString &input);

class Parser {
public:
  Parser(std::unique_ptr<RootSink> target);
  Parser(RootSink *target);
  Parser(std::unique_ptr<NodeSink> target);
  Parser(NodeSink *target);
  status_t nextChar(char c);

private:
  status_t charInString(char c, int cstate, int estate);
  status_t charInNumber(bool neg, char c, int cstate, int estate);
  status_t charInEsc(char c, int cstate, int estate);
  status_t charInBool(const char *t, bool v, char c, int cstate, int estate);
  status_t charInNull(char c, int cstate, int estate);
  std::unique_ptr<RootSink> target;
  int state = 0;
  int state2 = 0;
  int16 escape = 0;
  int16 highsurrogate = 0;
  BString token;
  BString unescaped;
  BString rawname;
  BString name;
  std::vector<int> stack;
  long long k = 0, s = 0, e = 0, z = 0;
};

} // namespace JSON
#endif // JSON_H
