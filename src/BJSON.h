#include "JSON.h"
#include <Message.h>

namespace JSON {
void fromBMessage(RootSink *target, const BMessage *source);
void fromBMessageArray(RootSink *target, const BMessage *source);
void fromBMessageObject(RootSink *target, const BMessage *source);

class BMessageDocSink : public NodeSink {
public:
  BMessageDocSink(BMessage *target);
  std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                      const BString &name) override;
  std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                     const BString &name) override;

private:
  BMessage *target;
};

class BMessageObjectDocSink : public NodeSink {
public:
  BMessageObjectDocSink(BMessage *target);
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
  BMessage *target;
};

class BMessageArrayDocSink : public NodeSink {
public:
  BMessageArrayDocSink(BMessage *target);
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
  BString key();
  BMessage *target;
  int32 counter;
};
} // namespace JSON
