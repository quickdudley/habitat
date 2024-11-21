#ifndef CONTACTSTORE_H
#define CONTACTSTORE_H

#include <Handler.h>
#include <sqlite3.h>

class ContactStore : public BHandler {
public:
  ContactStore(sqlite3 *database);
  void MessageReceived(BMessage *message) override;
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;

private:
  sqlite3 *database;
};

#endif
