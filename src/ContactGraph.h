#ifndef CONTACTGRAPH_H
#define CONTACTGRAPH_H

#include "Updatable.h"
#include <Handler.h>
#include <String.h>
#include <map>
#include <set>
#include <vector>

struct ContactSelection {
  std::set<BString> selected;
  std::set<BString> blocked;
  std::set<BString> own;
};

struct ContactLinkState {
  ContactLinkState();
  Updatable<bool> following;
  Updatable<bool> blocking;
  Updatable<bool> pub;
};

class ContactGraph : public BHandler {
public:
  ContactGraph();
  void MessageReceived(BMessage *message) override;

private:
  void logContact(BMessage *message);
  void sendState(BMessage *request);
  std::map<BString, std::map<BString, ContactLinkState>> graph;
  std::vector<BMessage *> pending;
  bool ready = false;
};

#endif
