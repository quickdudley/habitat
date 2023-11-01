#include "JSON.h"
#include <Message.h>

namespace JSON {
void fromBMessage(RootSink *target, const BMessage *source);
void fromBMessageArray(RootSink *target, const BMessage *source);
void fromBMessageObject(RootSink *target, const BMessage *source);

class BMessageDocSink : public NodeSink {
public:
  BMessageDocSink(BMessage *target);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  BMessage *target;
};

class BMessageObjectDocSink : public NodeSink {
public:
  BMessageObjectDocSink(BMessage *target);
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  BMessage *target;
};

class BMessageArrayDocSink : public NodeSink {
public:
  BMessageArrayDocSink(BMessage *target);
  void addNumber(BString &rawname, BString &name, BString &raw, number value);
  void addBool(BString &rawname, BString &name, bool value);
  void addNull(BString &rawname, BString &name);
  void addString(BString &rawname, BString &name, BString &raw, BString &value);
  std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name);
  std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name);

private:
  BString key();
  BMessage *target;
  int32 counter;
};
} // namespace JSON
