#include "BJSON.h"
#include <File.h>
#include <limits>
#include <set>
#include <iostream>

namespace JSON {

bool wasArray(const BMessage *msg) {
  if (msg->what == 'JSAR')
    return true;
  if (msg->what == 'JSOB')
    return false;
  std::set<int> collected;
  char *attrname;
  type_code attrtype;
  int32 index = 0;
  while (msg->GetInfo(B_ANY_TYPE, index, &attrname, &attrtype) == B_OK) {
    int built = 0;
    for (int i = 0; attrname[i] != 0; i++) {
      if (attrname[i] >= '0' && attrname[i] <= '9')
        built = built * 10 + (attrname[i] - '0');
      else
        return false;
    }
    collected.insert(built);
  }
  if (collected.size() == 0)
    return false;
  for (uint32 i = 0; i < collected.size(); i++) {
    if (collected.count(i) != 1)
      return false;
  }
  return true;
}

// TODO: escape nulls
static inline BString jsonAttrName(const BString &attrName) {
  if (attrName.EndsWith("_")) {
    BString substr;
    attrName.CopyInto(substr, 0, attrName.Length() - 1);
    return substr;
  } else {
    return attrName;
  }
}

static inline BString messageAttrName(const BString &attrName) {
  if (attrName == "specifiers" || attrName == "refs" ||
      attrName.EndsWith("_")) {
    BString padded(attrName);
    padded.Append('_', 1);
    return padded;
  } else {
    return attrName;
  }
}

void fromBMessageData(RootSink *target, const BMessage *source,
                      BString &attrname, type_code attrtype) {
  if (attrname == "specifiers" || attrname == "refs")
    return;
  switch (attrtype) {
  case B_BOOL_TYPE:
    target->addBool(jsonAttrName(attrname),
                    source->GetBool(attrname.String(), 0, false));
    break;
  case B_CHAR_TYPE: {
    char *data;
    ssize_t size;
    if (source->FindData(attrname.String(), B_CHAR_TYPE, (const void **)&data,
                         &size) == B_OK) {
      BString c;
      c.Append(data[0], 1);
      target->addString(jsonAttrName(attrname), c);
    }
  } break;
  case B_DOUBLE_TYPE:
    target->addNumber(jsonAttrName(attrname),
                      source->GetDouble(attrname.String(), 0.0));
    break;
  case B_FLOAT_TYPE:
    target->addNumber(jsonAttrName(attrname),
                      source->GetFloat(attrname.String(), 0.0));
    break;
  case B_INT64_TYPE:
    target->addNumber(jsonAttrName(attrname),
                      source->GetInt64(attrname.String(), 0.0));
    break;
  case B_MESSAGE_TYPE: {
    BMessage value;
    if (source->FindMessage(attrname, &value) == B_OK) {
      if (wasArray(&value)) {
        target->beginArray(jsonAttrName(attrname));
        fromBMessageArray(target, &value);
      } else {
        target->beginObject(jsonAttrName(attrname));
        fromBMessageObject(target, &value);
      }
      target->closeNode();
    }
  } break;
  case 'NULL':
    target->addNull(jsonAttrName(attrname));
    break;
  case B_REF_TYPE: {
    entry_ref ref;
    if (source->FindRef(attrname.String(), &ref) != B_OK)
      return;
    BFile text(&ref, B_READ_ONLY);
    // TODO: handle JSON files differently
    BString value;
    char buffer[1025];
    ssize_t length;
    while (true) {
      length = text.Read(buffer, 1024);
      if (length > 0) {
        buffer[length] = 0;
        value.Append(buffer);
      } else {
        break;
      }
    }
    target->addString(jsonAttrName(attrname), value);
  } break;
  case B_STRING_TYPE: {
  	BString value0;
  	source->FindString(attrname, &value0);
    BString value(source->GetString(attrname.String(), ""));
    if (value0 != value)
      std::cerr << value0.String() << std::endl;
    target->addString(jsonAttrName(attrname), value);
  } break;
  }
}

void fromBMessageObject(RootSink *target, const BMessage *source) {
  char *attrname;
  type_code attrtype;
  int32 index = 0;
  while (source->GetInfo(B_ANY_TYPE, index, &attrname, &attrtype) == B_OK) {
    BString name(attrname);
    fromBMessageData(target, source, name, attrtype);
    index++;
  }
}

void fromBMessage(RootSink *target, const BMessage *source) {
  BString blank;
  if (wasArray(source)) {
    target->beginArray(blank);
    fromBMessageArray(target, source);
  } else {
    target->beginObject(blank);
    fromBMessageObject(target, source);
  }
  target->closeNode();
}

void fromBMessageArray(RootSink *target, const BMessage *source) {
  for (int32 index = 0;; index++) {
    BString key;
    key << index;
    type_code attrtype;
    if (source->GetInfo(key.String(), &attrtype) == B_OK)
      fromBMessageData(target, source, key, attrtype);
    else
      break;
  }
}

class BMessageObjectChild : public BMessageObjectDocSink {
public:
  BMessageObjectChild(BMessage *parent, const BString &key);
  ~BMessageObjectChild();

private:
  BMessage *parent;
  BMessage target;
  BString key;
};

class BMessageArrayChild : public BMessageArrayDocSink {
public:
  BMessageArrayChild(BMessage *parent, const BString &key);
  ~BMessageArrayChild();

private:
  BMessage *parent;
  BMessage target;
  BString key;
};

BMessageObjectChild::BMessageObjectChild(BMessage *parent, const BString &key)
    :
    BMessageObjectDocSink(&target),
    parent(parent),
    key(key) {
  this->target.what = 'JSOB';
}

BMessageObjectChild::~BMessageObjectChild() {
  parent->AddMessage(this->key.String(), &this->target);
}

BMessageArrayChild::BMessageArrayChild(BMessage *parent, const BString &key)
    :
    BMessageArrayDocSink(&target),
    parent(parent),
    key(key) {
  this->target.what = 'JSAR';
}

BMessageArrayChild::~BMessageArrayChild() {
  parent->AddMessage(this->key.String(), &(this->target));
}

BMessageDocSink::BMessageDocSink(BMessage *target)
    :
    target(target) {}

std::unique_ptr<NodeSink> BMessageDocSink::addObject(const BString &rawname,
                                                     const BString &name) {
  return std::make_unique<BMessageObjectDocSink>(this->target);
}

std::unique_ptr<NodeSink> BMessageDocSink::addArray(const BString &rawname,
                                                    const BString &name) {
  return std::make_unique<BMessageArrayDocSink>(this->target);
}

BMessageObjectDocSink::BMessageObjectDocSink(BMessage *target)
    :
    target(target) {
  target->what = 'JSOB';
}

void BMessageObjectDocSink::addNumber(const BString &rawname,
                                      const BString &name, const BString &raw,
                                      number value) {
  this->target->AddDouble(messageAttrName(name).String(), value);
}

void BMessageObjectDocSink::addBool(const BString &rawname, const BString &name,
                                    bool value) {
  this->target->AddBool(messageAttrName(name).String(), value);
}

void BMessageObjectDocSink::addNull(const BString &rawname,
                                    const BString &name) {
  char pad = 0;
  this->target->AddData(messageAttrName(name).String(), 'NULL', &pad, 1);
}

void BMessageObjectDocSink::addString(const BString &rawname,
                                      const BString &name, const BString &raw,
                                      const BString &value) {
  this->target->AddString(messageAttrName(name), value);
}

std::unique_ptr<NodeSink>
BMessageObjectDocSink::addObject(const BString &rawname, const BString &name) {
  return std::make_unique<BMessageObjectChild>(this->target,
                                               messageAttrName(name));
}

std::unique_ptr<NodeSink>
BMessageObjectDocSink::addArray(const BString &rawname, const BString &name) {
  return std::make_unique<BMessageArrayChild>(this->target,
                                              messageAttrName(name));
}

BMessageArrayDocSink::BMessageArrayDocSink(BMessage *target)
    :
    target(target),
    counter(0) {
  target->what = 'JSAR';
}

void BMessageArrayDocSink::addNumber(const BString &rawname,
                                     const BString &name, const BString &raw,
                                     number value) {
  this->target->AddDouble(this->key().String(), value);
}

void BMessageArrayDocSink::addBool(const BString &rawname, const BString &name,
                                   bool value) {
  this->target->AddBool(this->key().String(), value);
}

void BMessageArrayDocSink::addNull(const BString &rawname,
                                   const BString &name) {
  char pad = 0;
  this->target->AddData(this->key().String(), 'NULL', &pad, 1);
}

void BMessageArrayDocSink::addString(const BString &rawname,
                                     const BString &name, const BString &raw,
                                     const BString &value) {
  this->target->AddString(this->key().String(), value.String());
}

std::unique_ptr<NodeSink>
BMessageArrayDocSink::addObject(const BString &rawname, const BString &name) {
  BString key = this->key();
  return std::make_unique<BMessageObjectChild>(this->target, key);
}

std::unique_ptr<NodeSink> BMessageArrayDocSink::addArray(const BString &rawname,
                                                         const BString &name) {
  BString key = this->key();
  return std::make_unique<BMessageArrayChild>(this->target, key);
}

BString BMessageArrayDocSink::key() {
  BString result;
  result << this->counter++;
  return result;
}
} // namespace JSON
