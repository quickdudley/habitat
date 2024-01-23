#ifndef SELECT_CONTACTS_H
#define SELECT_CONTACTS_H

#include <Handler.h>
#include <Message.h>
#include <Messenger.h>
#include <set>

class SelectContacts : public BHandler {
public:
  SelectContacts(const BMessenger &db, const BMessenger &graph);
  void MessageReceived(BMessage *message);

private:
  void makeSelection(BMessage *graph);
  BMessenger db;
  BMessenger graph;
  std::set<BString> current;
  bool fetching = false;
};

#endif // SELECT_CONTACTS_H
