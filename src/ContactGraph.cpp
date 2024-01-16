#include "ContactGraph.h"
#include <Looper.h>

ContactSelection &ContactSelection::operator+=(const ContactSelection &other) {
#define MERGE_IN(prop)                                                         \
  for (auto item : other.prop)                                                 \
  this->prop.insert(item)
  MERGE_IN(selected);
  MERGE_IN(blocked);
  MERGE_IN(own);
#undef MERGE_IN
  return *this;
}

ContactSelection
ContactSelection::operator+(const ContactSelection &other) const {
  ContactSelection result;
  result += *this;
  result += other;
  return result;
}

std::set<BString> ContactSelection::combine() const {
  std::set<BString> result;
  for (auto item : this->selected)
    result.insert(item);
  for (auto item : this->blocked)
    result.erase(item);
  for (auto item : this->own)
    result.insert(item);
  return result;
}

ContactLinkState::ContactLinkState()
    :
    following(false),
    blocking(false),
    pub(false) {}

ContactGraph::ContactGraph() {}

void ContactGraph::MessageReceived(BMessage *message) {
  switch (message->what) {
  case B_GET_PROPERTY:
    if (this->ready)
      this->sendState(message);
    else
      this->pending.push_back(this->Looper()->DetachCurrentMessage());
    break;
  case 'DONE':
    this->ready = true;
    this->SendNotices('CTAC');
    for (auto rq : this->pending) {
      this->sendState(rq);
      delete rq;
    }
    this->pending.clear();
    this->pending.shrink_to_fit();
    break;
  case 'JSOB':
    return logContact(message);
  default:
    return BHandler::MessageReceived(message);
  }
}

void ContactGraph::logContact(BMessage *message) {
  int64 sequence;
  {
    double sq;
    if (message->FindDouble("sequence", &sq) != B_OK)
      return;
    sequence = (int64)sq;
  }
  BString author;
  if (message->FindString("author", &author) != B_OK)
    return;
  BMessage content;
  if (message->FindMessage("content", &content) != B_OK)
    return;
  BString type;
  if (content.FindString("type", &type) != B_OK || type != "contact")
    return;
  BString contact;
  if (content.FindString("contact", &contact) != B_OK)
    return;
  this->graph.try_emplace(author, std::map<BString, ContactLinkState>());
  auto &node = this->graph.find(author)->second;
  node.try_emplace(contact, ContactLinkState());
  auto &edge = node.find(contact)->second;
  bool changed = false;
  edge.following.check(
      [&](auto &oldValue) {
        bool value = oldValue;
        if (content.FindBool("following", &value) == B_OK) {
          changed = true;
          oldValue = value;
          return true;
        } else {
          return false;
        }
      },
      sequence);
  edge.blocking.check(
      [&](auto &oldValue) {
        bool value = oldValue;
        if (content.FindBool("blocking", &value) == B_OK) {
          changed = true;
          oldValue = value;
          return true;
        } else {
          return false;
        }
      },
      sequence);
  edge.pub.check(
      [&](auto &oldValue) {
        bool value = oldValue;
        if (content.FindBool("pub", &value) == B_OK) {
          changed = true;
          oldValue = value;
          return true;
        } else {
          return false;
        }
      },
      sequence);
  if (changed && this->ready)
    this->SendNotices('CTAC');
}

void ContactGraph::sendState(BMessage *request) {
  BMessage reply(B_REPLY);
  BMessage result;
  for (const auto &[node, edges] : this->graph) {
    BMessage branch;
    for (const auto &[subject, edge] : edges) {
      BMessage leaf;
      leaf.AddBool("following", edge.following.peek());
      leaf.AddBool("blocked", edge.blocking.peek());
      leaf.AddBool("pub", edge.pub.peek());
      branch.AddMessage(subject, &leaf);
    }
    result.AddMessage(node, &branch);
  }
  reply.AddMessage("result", &result);
  reply.AddInt32("error", B_OK);
  request->SendReply(&reply);
}
