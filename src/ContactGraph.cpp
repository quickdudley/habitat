#include "ContactGraph.h"
#include <Looper.h>

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
  for (const auto &[node, edges] : this->graph) {
    BMessage branch;
    for (const auto &[subject, edge] : edges) {
      BMessage leaf;
      leaf.AddBool("following", edge.following.peek());
      leaf.AddBool("blocked", edge.blocking.peek());
      leaf.AddBool("pub", edge.pub.peek());
      branch.AddMessage(subject, &leaf);
    }
    reply.AddMessage(node, &branch);
  }
  request->SendReply(&reply);
}
