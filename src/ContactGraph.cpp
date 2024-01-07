#include "ContactGraph.h"

ContactLinkState::ContactLinkState()
    :
    following(false),
    blocking(false) {}

ContactGraph::ContactGraph() {}

void ContactGraph::MessageReceived(BMessage *message) {
  switch (message->what) {
  case 'DONE':
    this->ready = true;
    this->SendNotices('CTAC');
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
  if (changed && this->ready)
    this->SendNotices('CTAC');
}
