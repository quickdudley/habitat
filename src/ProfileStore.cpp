#include "ProfileStore.h"
#include <Messenger.h>
#include <PropertyInfo.h>
#include <String.h>
#include <cstring>
#include <set>

ProfileStore::ProfileStore(sqlite3 *database)
    :
    database(database) {}

enum { kProfile };

static property_info properties[] = {{"Profile",
                                      {B_GET_PROPERTY, 0},
                                      {B_NAME_SPECIFIER, 0},
                                      "Profile details",
                                      kProfile,
                                      {}},
                                     {0}};

status_t ProfileStore::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat+profilestore");
  BPropertyInfo propertyInfo(properties);
  data->AddFlat("messages", &propertyInfo);
  return BHandler::GetSupportedSuites(data);
}

BHandler *ProfileStore::ResolveSpecifier(BMessage *msg, int32 index,
                                         BMessage *specifier, int32 what,
                                         const char *property) {
  return this;
}

void ProfileStore::MessageReceived(BMessage *message) {
  if (message->HasSpecifiers()) {
    BMessage reply(B_REPLY);
    status_t error = B_ERROR;
    int32 index;
    BMessage specifier;
    int32 what;
    const char *property;
    uint32 match;
    if (message->GetCurrentSpecifier(&index, &specifier, &what, &property) !=
        B_OK) {
      return BHandler::MessageReceived(message);
    }
    BPropertyInfo propertyInfo(properties);
    propertyInfo.FindMatch(message, index, &specifier, what, property, &match);
    switch (match) {
    case kProfile: {
    } break;
    default:
      return BHandler::MessageReceived(message);
    }
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    if (message->ReturnAddress().IsValid())
      message->SendReply(&reply);
    return;
  } else if (BString author; message->FindString("author", &author) == B_OK) {
    if (BMessage content; message->FindMessage("content", &content) == B_OK ||
        message->FindMessage("cleartext", &content) == B_OK) {
      int64 sequence;
      if (double sq; message->FindDouble("sequence", &sq) == B_OK)
        sequence = (int64)sq;
      else
        goto super;
      if (BString about;
          content.FindString("about", &about) != B_OK || about != author) {
        goto super;
      }
      std::set<BString> toInsert;
      std::set<BString> toUpdate;
      bool successful = true;
      sqlite3_exec(this->database, "BEGIN TRANSACTION;", NULL, NULL, NULL);
      {
        int32 index = 0;
        char *attrname;
        type_code attrtype;
        sqlite3_stmt *qry;
        sqlite3_prepare_v2(this->database,
                           "SELECT sequence FROM profiles WHERE author = ? "
                           "AND property = ?",
                           -1, &qry, NULL);
        sqlite3_bind_text(qry, 1, author.String(), author.Length(),
                          SQLITE_STATIC);
        while (content.GetInfo(B_ANY_TYPE, index, &attrname, &attrtype) ==
               B_OK) {
          if (std::strcmp(attrname, "about") == 0) {
            index++;
            continue;
          }
          sqlite3_bind_text(qry, 2, attrname, -1, SQLITE_TRANSIENT);
          switch (sqlite3_step(qry)) {
          case SQLITE_DONE:
            toInsert.emplace(attrname);
            break;
          case SQLITE_ROW:
            if (sqlite3_column_int64(qry, 0) < sequence)
              toUpdate.emplace(attrname);
            break;
          default:
            successful = false;
          }
          sqlite3_reset(qry);
          index++;
        }
        sqlite3_finalize(qry);
      }
      if (!toInsert.empty()) {
        sqlite3_stmt *qry;
        sqlite3_prepare_v2(
            this->database,
            "INSERT INTO profiles(author, property, sequence, type, value) "
            "VALUES(?, ?, ?, ?, ?)",
            -1, &qry, NULL);
        sqlite3_bind_text(qry, 1, author.String(), author.Length(),
                          SQLITE_STATIC);
        sqlite3_bind_int64(qry, 3, sequence);
        for (const BString &attr : toInsert) {
          bool fixedSize;
          type_code typeFound;
          if (content.GetInfo(attr.String(), &typeFound, &fixedSize) == B_OK) {
            sqlite3_bind_text(qry, 2, attr.String(), attr.Length(),
                              SQLITE_STATIC);
            sqlite3_bind_int64(qry, 4, typeFound);
            const void *data;
            ssize_t numBytes;
            content.FindData(attr.String(), typeFound, &data, &numBytes);
            sqlite3_bind_blob(qry, 5, data, numBytes, SQLITE_STATIC);
            if (sqlite3_step(qry) != SQLITE_DONE)
              successful = false;
            sqlite3_reset(qry);
          }
        }
        sqlite3_finalize(qry);
      }
      if (!toUpdate.empty()) {
        sqlite3_stmt *qry;
        sqlite3_prepare_v2(
            this->database,
            "UPDATE profiles SET sequence = ?, type = ?, value = ? "
            "WHERE author = ? "
            "AND property = ?",
            -1, &qry, NULL);
        sqlite3_bind_int64(qry, 1, sequence);
        sqlite3_bind_text(qry, 4, author.String(), author.Length(),
                          SQLITE_STATIC);
        for (const BString &attr : toUpdate) {
          bool fixedSize;
          type_code typeFound;
          if (content.GetInfo(attr.String(), &typeFound, &fixedSize) == B_OK) {
            sqlite3_bind_text(qry, 5, attr.String(), attr.Length(),
                              SQLITE_STATIC);
            sqlite3_bind_int64(qry, 2, typeFound);
            const void *data;
            ssize_t numBytes;
            content.FindData(attr.String(), typeFound, &data, &numBytes);
            sqlite3_bind_blob(qry, 3, data, numBytes, SQLITE_STATIC);
            if (sqlite3_step(qry) != SQLITE_DONE)
              successful = false;
            sqlite3_reset(qry);
          }
        }
        sqlite3_finalize(qry);
      }
      if (successful) {
        if (BString cypherkey;
            message->FindString("cypherkey", &cypherkey) == B_OK) {
          sqlite3_stmt *qry;
          sqlite3_prepare_v2(
              this->database,
              "UPDATE messages SET processed = 1 WHERE cypherkey = ?", -1, &qry,
              NULL);
          sqlite3_bind_text(qry, 1, cypherkey.String(), cypherkey.Length(),
                            SQLITE_STATIC);
          sqlite3_step(qry);
          sqlite3_finalize(qry);
        }
      }
      sqlite3_exec(this->database, "END TRANSACTION;", NULL, NULL, NULL);
    } else {
      goto super;
    }
  } else {
  super:
    BHandler::MessageReceived(message);
  }
}
