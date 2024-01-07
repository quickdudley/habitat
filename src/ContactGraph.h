#ifndef CONTACTGRAPH_H
#define CONTACTGRAPH_H

#include "Updatable.h"
#include <Handler.h>
#include <String.h>
#include <map>

struct ContactLinkState {
  ContactLinkState();
  Updatable<bool> following;
  Updatable<bool> blocking;
};

class ContactGraph : public BHandler {
public:
  ContactGraph();
  void MessageReceived(BMessage *message) override;

private:
  void logContact(BMessage *message);
  std::map<BString, std::map<BString, ContactLinkState>> graph;
  bool ready = false;
};

#endif
