#include "ContactGraph.h"

ContactGraph::ContactGraph() {}

void ContactGraph::MessageReceived(BMessage *message) {
  switch (message->what) {
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
  this->graph.try_emplace(
      author, std::map<BString, std::tuple<int64, bool, int64, bool>>());
  auto &node = this->graph.find(author)->second;
  node.try_emplace(contact,
                   std::tuple<int64, bool, int64, bool>(-1, false, -1, false));
  auto &edge = node.find(contact)->second;
  bool value = false;
  if (sequence > std::get<0>(edge) &&
      content.FindBool("following", &value) == B_OK) {
    std::get<0>(edge) = sequence;
    std::get<1>(edge) = value;
  }
  if (sequence > std::get<2>(edge) &&
      content.FindBool("blocking", &value) == B_OK) {
    std::get<2>(edge) = sequence;
    std::get<3>(edge) = value;
  }
}
