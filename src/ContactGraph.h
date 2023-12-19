#ifndef CONTACTGRAPH_H
#define CONTACTGRAPH_H

#include <Handler.h>
#include <String.h>
#include <map>
#include <tuple>

class ContactGraph : public BHandler {
public:
  ContactGraph();
  void MessageReceived(BMessage *message) override;

private:
  void logContact(BMessage *message);
  std::map<BString, std::map<BString, std::tuple<int64, bool, int64, bool>>>
      graph;
};

#endif
