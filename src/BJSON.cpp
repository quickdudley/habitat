#include "BJSON.h"

namespace JSON {
class BMessageObjectChild : public BMessageObjectDocSink {
public:
  BMessageObjectChild(BMessage *parent, BString &key);
  ~BMessageObjectChild();

private:
  BMessage *parent;
  BMessage target;
  BString key;
};

class BMessageArrayChild : public BMessageObjectDocSink {
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
    key(key) {}

BMessageObjectChild::~BMessageObjectChild() {
  parent->AddMessage(this->key.String(), &this->target);
}

BMessageArrayChild::BMessageArrayChild(BMessage *parent, BString &key)
    :
    BMessageObjectDocSink(&target),
    parent(parent),
    key(key) {}

BMessageArrayChild::~BMessageArrayChild() {
  parent->AddMessage(this->key.String(), &this->target);
}

BMessageDocSink::BMessageDocSink(BMessage *target)
    :
    target(target) {}

std::unique_ptr<NodeSink> BMessageDocSink::addObject(BString &rawname,
                                                     BString &name) {
  return std::unique_ptr<NodeSink>(new BMessageObjectDocSink(this->target));
}

std::unique_ptr<NodeSink> BMessageDocSink::addArray(BString &rawname,
                                                    BString &name) {
  return std::unique_ptr<NodeSink>(new BMessageArrayDocSink(this->target));
}

BMessageObjectDocSink::BMessageObjectDocSink(BMessage *target)
    :
    target(target) {}

void BMessageObjectDocSink::addNumber(BString &rawname, BString &name,
                                      BString &raw, number value) {
  this->target->AddDouble(name.String(), value);
}

void BMessageObjectDocSink::addBool(BString &rawname, BString &name,
                                    bool value) {
  this->target->AddBool(name.String(), value);
}

void BMessageObjectDocSink::addNull(BString &rawname, BString &name) {
  this->target->AddData(name.String(), 'NULL', NULL, 0);
}

void BMessageObjectDocSink::addString(BString &rawname, BString &name,
                                      BString &raw, BString &value) {
  this->target->AddString(name.String(), value.String());
}

std::unique_ptr<NodeSink> BMessageObjectDocSink::addObject(BString &rawname,
                                                           BString &name) {
  return std::unique_ptr<NodeSink>(new BMessageObjectChild(this->target, name));
}

std::unique_ptr<NodeSink> BMessageObjectDocSink::addArray(BString &rawname,
                                                          BString &name) {
  return std::unique_ptr<NodeSink>(new BMessageArrayChild(this->target, name));
}

BMessageArrayDocSink::BMessageArrayDocSink(BMessage *target)
    :
    target(target),
    counter(0) {}

// here

void BMessageArrayDocSink::addNumber(BString &rawname, BString &name,
                                     BString &raw, number value) {
  this->target->AddDouble(this->key().String(), value);
}

void BMessageArrayDocSink::addBool(BString &rawname, BString &name,
                                   bool value) {
  this->target->AddBool(this->key().String(), value);
}

void BMessageArrayDocSink::addNull(BString &rawname, BString &name) {
  this->target->AddData(this->key().String(), 'NULL', NULL, 0);
}

void BMessageArrayDocSink::addString(BString &rawname, BString &name,
                                     BString &raw, BString &value) {
  this->target->AddString(this->key().String(), value.String());
}

std::unique_ptr<NodeSink> BMessageArrayDocSink::addObject(BString &rawname,
                                                          BString &name) {
  BString key = this->key();
  return std::unique_ptr<NodeSink>(new BMessageObjectChild(this->target, key));
}

std::unique_ptr<NodeSink> BMessageArrayDocSink::addArray(BString &rawname,
                                                         BString &name) {
  BString key = this->key();
  return std::unique_ptr<NodeSink>(new BMessageArrayChild(this->target, key));
}

BString BMessageArrayDocSink::key() {
  BString result;
  result << this->counter++;
  return result;
}
} // namespace JSON