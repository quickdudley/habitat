#ifndef JSON_H
#define JSON_H

#include <String.h>
#include <memory>
#include <vector>

namespace JSON {

typedef long double number;

BString escapeString(BString src);

class NodeSink {
public:
  virtual ~NodeSink();
  virtual void addNumber(BString rawname, BString name, BString raw,
                         number value) = 0;
  virtual void addBool(BString rawname, BString name, bool value) = 0;
  virtual void addNull(BString rawname, BString name) = 0;
  virtual void addString(BString rawname, BString name, BString raw,
                         BString value) = 0;
  virtual std::unique_ptr<NodeSink> addObject(BString rawname,
                                              BString name) = 0;
  virtual std::unique_ptr<NodeSink> addArray(BString rawname, BString name) = 0;
};

class IgnoreNode : public NodeSink {
public:
  void addNumber(BString rawname, BString name, BString raw, number value);
  void addBool(BString rawname, BString name, bool value);
  void addNull(BString rawname, BString name);
  void addString(BString rawname, BString name, BString raw, BString value);
  std::unique_ptr<NodeSink> addObject(BString rawname, BString name);
  std::unique_ptr<NodeSink> addArray(BString rawname, BString name);
};

class SerializerStart : public NodeSink {
public:
  SerializerStart(BString *target);
  void addNumber(BString rawname, BString name, BString raw, number value);
  void addBool(BString rawname, BString name, bool value);
  void addNull(BString rawname, BString name);
  void addString(BString rawname, BString name, BString raw, BString value);
  std::unique_ptr<NodeSink> addObject(BString rawname, BString name);
  std::unique_ptr<NodeSink> addArray(BString rawname, BString name);

private:
  BString *target;
};

class RootSink {
public:
  RootSink(std::unique_ptr<NodeSink> rootConsumer);
  RootSink(NodeSink *rootConsumer);
  virtual ~RootSink();
  void addNumber(BString rawname, BString name, BString raw, number value);
  void addNumber(BString name, long double value);
  void addBool(BString rawname, BString name, bool value);
  void addBool(BString name, bool value);
  void addNull(BString rawname, BString name);
  void addNull(BString name);
  void addString(BString rawname, BString name, BString raw, BString value);
  void addString(BString name, BString value);
  void beginObject(BString rawname, BString name);
  void beginObject(BString name);
  void beginArray(BString rawname, BString name);
  void beginArray(BString name);
  void closeNode();

private:
  std::vector<std::unique_ptr<NodeSink>> stack;
};

class Parser {
public:
  Parser(std::unique_ptr<RootSink> target);
  Parser(RootSink *target);
  Parser(std::unique_ptr<NodeSink> target);
  Parser(NodeSink *target);
  status_t nextChar(char c);

private:
  status_t charInString(char c, int cstate, int estate);
  status_t charInEsc(char c, int cstate, int estate);
  std::unique_ptr<RootSink> target;
  int state = 0;
  int state2 = 0;
  int escape = 0;
  BString token;
  BString unescaped;
  BString rawname;
  BString name;
  std::vector<int> stack;
};

} // namespace JSON
#endif // JSON_H
