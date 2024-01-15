#ifndef SELECT_CONTACTS_H
#define SELECT_CONTACTS_H

#include <Handler.h>
#include <Message.h>

class SelectContacts : public BHandler {
public:
  void MessageReceived(BMessage *message);
};

#endif // SELECT_CONTACTS_H
