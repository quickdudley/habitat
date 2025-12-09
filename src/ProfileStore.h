#ifndef PROFILE_STORE_H
#define PROFILE_STORE_H

#include <Handler.h>
#include <sqlite3.h>

class ProfileStore : public BHandler {
public:
  ProfileStore(sqlite3 *database);
  status_t GetSupportedSuites(BMessage *data) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void MessageReceived(BMessage *message) override;

private:
  sqlite3 *database;
};

#endif // PROFILE_STORE_H
