#ifndef JSON_H
#define JSON_H

#include <DataIO.h>
#include <String.h>
#include <memory>
#include <vector>

namespace JSON {

typedef double number;

BString escapeString(const BString &src);
BString stringifyNumber(number value);

class NodeSink {
public:
  virtual ~NodeSink();
  virtual void addNumber(const BString &rawname, const BString &name,
                         const BString &raw, number value);
  virtual void addBool(const BString &rawname, const BString &name, bool value);
  virtual void addNull(const BString &rawname, const BString &name);
  virtual void addString(const BString &rawname, const BString &name,
                         const BString &raw, const BString &value);
  virtual std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                              const BString &name);
  virtual std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                             const BString &name);
};

typedef NodeSink IgnoreNode;

class SerializerStart : public NodeSink {
public:
  SerializerStart(BString *target, int32 indentation = 2, bool newlines = true);
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
  BString *target;
  int32 indentation;
  bool newlines;
};

class Splitter : public NodeSink {
public:
  Splitter(std::unique_ptr<NodeSink> a, std::unique_ptr<NodeSink> b);
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
  std::unique_ptr<NodeSink> a;
  std::unique_ptr<NodeSink> b;
};

class RootSink {
public:
  RootSink(std::unique_ptr<NodeSink> rootConsumer);
  RootSink(NodeSink *rootConsumer);
  virtual ~RootSink();
  void addNumber(const BString &rawname, const BString &name,
                 const BString &raw, number value);
  void addNumber(const BString &name, number value);
  void addNumber(const char *name, number value);
  void addBool(const BString &rawname, const BString &name, bool value);
  void addBool(const BString &name, bool value);
  void addBool(const char *name, bool value);
  void addNull(const BString &rawname, const BString &name);
  void addNull(const BString &name);
  void addNull(const char *name);
  void addString(const BString &rawname, const BString &name,
                 const BString &raw, const BString &value);
  void addString(const BString &name, const BString &value);
  void beginObject(const BString &rawname, const BString &name);
  void beginObject(const BString &name);
  void beginArray(const BString &rawname, const BString &name);
  void beginArray(const BString &name);
  void closeNode();

private:
  std::vector<std::unique_ptr<NodeSink>> stack;
};

class Parser;

status_t parse(std::unique_ptr<NodeSink> target, BDataIO *input);
status_t parse(NodeSink *target, BDataIO *input);
status_t parse(std::unique_ptr<NodeSink> target, BDataIO *input, size_t bytes);
status_t parse(NodeSink *target, BDataIO *input, size_t bytes);
status_t parse(Parser *target, BDataIO *input);
status_t parse(Parser *target, BDataIO *input, size_t bytes);
status_t parse(std::unique_ptr<NodeSink> target, const char *input);
status_t parse(NodeSink *target, const char *input);
status_t parse(std::unique_ptr<NodeSink> target, const char *input,
               size_t bytes);
status_t parse(NodeSink *target, const char *input, size_t bytes);
status_t parse(Parser *target, const char *input);
status_t parse(Parser *target, const char *input, size_t bytes);
status_t parse(std::unique_ptr<NodeSink> target, const BString &input);
status_t parse(NodeSink *target, const BString &input);
status_t parse(Parser *target, const BString &input);

class Parser {
public:
  Parser(std::unique_ptr<RootSink> target, bool lax = false);
  Parser(RootSink *target, bool lax = false);
  Parser(std::unique_ptr<NodeSink> target, bool lax = false);
  Parser(NodeSink *target, bool lax = false);
  status_t nextChar(char c);
  void setPropName(const BString &name);

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
  bool lax;
};

} // namespace JSON
#endif // JSON_H
