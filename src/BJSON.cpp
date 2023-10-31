#include "BJSON.h"
#include <File.h>
#include <limits>
#include <set>

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
  for (int i = 0; i < collected.size(); i++) {
    if (collected.count(i) != 1)
      return false;
  }
  return true;
}

void fromBMessageData(RootSink *target, const BMessage *source,
                      BString &attrname, type_code attrtype) {
  if (attrname == "specifiers" || attrname == "refs")
    return;
  switch (attrtype) {
  case B_BOOL_TYPE:
    target->addBool(attrname, source->GetBool(attrname.String(), 0, false));
    break;
  case B_CHAR_TYPE: {
    char *data;
    ssize_t size;
    if (source->FindData(attrname.String(), B_CHAR_TYPE, (const void **)&data,
                         &size) == B_OK) {
      BString c;
      c.Append(data[0], 1);
      target->addString(attrname, c);
    }
  } break;
  case B_DOUBLE_TYPE:
    target->addNumber(attrname, source->GetDouble(attrname.String(), 0.0));
    break;
  case B_FLOAT_TYPE:
    target->addNumber(attrname, source->GetFloat(attrname.String(), 0.0));
    break;
  case B_INT64_TYPE:
    target->addNumber(attrname, source->GetInt64(attrname.String(), 0.0));
    break;
  case B_MESSAGE_TYPE: {
    BMessage value;
    if (source->FindMessage(attrname, &value) == B_OK) {
      if (wasArray(&value)) {
        target->beginArray(attrname);
        fromBMessageArray(target, &value);
      } else {
        target->beginObject(attrname);
        fromBMessageObject(target, &value);
      }
      target->closeNode();
    }
  } break;
  case 'NULL':
    target->addNull(attrname);
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
    target->addString(attrname, value);
  } break;
  case B_STRING_TYPE: {
    BString value(source->GetString(attrname.String(), ""));
    target->addString(attrname, value);
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
  BMessageObjectChild(BMessage *parent, BString &key);
  ~BMessageObjectChild();

private:
  BMessage *parent;
  BMessage target;
  BString key;
};

class BMessageArrayChild : public BMessageArrayDocSink {
public:
  BMessageArrayChild(BMessage *parent, BString &key);
  ~BMessageArrayChild();

private:
  BMessage *parent;
  BMessage target;
  BString key;
};

BMessageObjectChild::BMessageObjectChild(BMessage *parent, BString &key)
    :
    BMessageObjectDocSink(&target),
    parent(parent),
    key(key) {
  this->target.what = 'JSOB';
}

BMessageObjectChild::~BMessageObjectChild() {
  parent->AddMessage(this->key.String(), &this->target);
}

BMessageArrayChild::BMessageArrayChild(BMessage *parent, BString &key)
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

std::unique_ptr<NodeSink> BMessageDocSink::addObject(BString &rawname,
                                                     BString &name) {
  return std::make_unique<BMessageObjectDocSink>(this->target);
}

std::unique_ptr<NodeSink> BMessageDocSink::addArray(BString &rawname,
                                                    BString &name) {
  return std::make_unique<BMessageArrayDocSink>(this->target);
}

BMessageObjectDocSink::BMessageObjectDocSink(BMessage *target)
    :
    target(target) {
  target->what = 'JSOB';
}

void BMessageObjectDocSink::addNumber(BString &rawname, BString &name,
                                      BString &raw, number value) {
  this->target->AddDouble(name.String(), value);
}

void BMessageObjectDocSink::addBool(BString &rawname, BString &name,
                                    bool value) {
  this->target->AddBool(name.String(), value);
}

void BMessageObjectDocSink::addNull(BString &rawname, BString &name) {
  char pad = 0;
  this->target->AddData(name.String(), 'NULL', &pad, 1);
}

void BMessageObjectDocSink::addString(BString &rawname, BString &name,
                                      BString &raw, BString &value) {
  this->target->AddString(name.String(), value.String());
}

std::unique_ptr<NodeSink> BMessageObjectDocSink::addObject(BString &rawname,
                                                           BString &name) {
  return std::make_unique<BMessageObjectChild>(this->target, name);
}

std::unique_ptr<NodeSink> BMessageObjectDocSink::addArray(BString &rawname,
                                                          BString &name) {
  return std::make_unique<BMessageArrayChild>(this->target, name);
}

BMessageArrayDocSink::BMessageArrayDocSink(BMessage *target)
    :
    target(target),
    counter(0) {
  target->what = 'JSAR';
}

void BMessageArrayDocSink::addNumber(BString &rawname, BString &name,
                                     BString &raw, number value) {
  this->target->AddDouble(this->key().String(), value);
}

void BMessageArrayDocSink::addBool(BString &rawname, BString &name,
                                   bool value) {
  this->target->AddBool(this->key().String(), value);
}

void BMessageArrayDocSink::addNull(BString &rawname, BString &name) {
  char pad = 0;
  this->target->AddData(this->key().String(), 'NULL', &pad, 1);
}

void BMessageArrayDocSink::addString(BString &rawname, BString &name,
                                     BString &raw, BString &value) {
  this->target->AddString(this->key().String(), value.String());
}

std::unique_ptr<NodeSink> BMessageArrayDocSink::addObject(BString &rawname,
                                                          BString &name) {
  BString key = this->key();
  return std::make_unique<BMessageObjectChild>(this->target, key);
}

std::unique_ptr<NodeSink> BMessageArrayDocSink::addArray(BString &rawname,
                                                         BString &name) {
  BString key = this->key();
  return std::make_unique<BMessageArrayChild>(this->target, key);
}

BString BMessageArrayDocSink::key() {
  BString result;
  result << this->counter++;
  return result;
}
} // namespace JSON
