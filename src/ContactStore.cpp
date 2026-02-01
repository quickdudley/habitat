#include "ContactStore.h"
#include "ContactGraph.h"
#include <Messenger.h>
#include <PropertyInfo.h>
#include <map>

ContactStore::ContactStore(sqlite3 *database)
    : database(database) {}

enum { kContact };

static property_info properties[] = {{"Contact",
                                      {B_GET_PROPERTY, 0},
                                      {B_DIRECT_SPECIFIER, 0},
                                      "Contact graph",
                                      kContact,
                                      {}},
                                     {"Contact",
                                      {B_SET_PROPERTY, 0},
                                      {B_NAME_SPECIFIER, 0},
                                      "Contact details",
                                      kContact,
                                      {}},
                                     {0}};

void ContactStore::MessageReceived(BMessage *message) {
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
    case kContact: {
      switch (message->what) {
      case B_GET_PROPERTY: {
        sqlite3_stmt *qry;
        sqlite3_prepare_v2(
            this->database,
            "SELECT author, contact, property, sequence, value FROM contacts",
            -1, &qry, NULL);
        std::map<BString, std::map<BString, ContactLinkState>> graph;
        while (sqlite3_step(qry) == SQLITE_ROW) {
          BString author = (const char *)sqlite3_column_text(qry, 0);
          BString contact = (const char *)sqlite3_column_text(qry, 1);
          BString property = (const char *)sqlite3_column_text(qry, 2);
          int64 sequence = sqlite3_column_int64(qry, 3);
          bool value = sqlite3_column_int64(qry, 4) != 0;
          graph.try_emplace(author, std::map<BString, ContactLinkState>());
          auto &node = graph.find(author)->second;
          node.try_emplace(contact, ContactLinkState());
          auto &edge = node.find(contact)->second;
          if (property == "following") {
            edge.following.check(
                [&](auto &oldValue) {
                  oldValue = value;
                  return true;
                },
                sequence);
          } else if (property == "blocking") {
            edge.blocking.check(
                [&](auto &oldValue) {
                  oldValue = value;
                  return true;
                },
                sequence);
          } else if (property == "pub") {
            edge.pub.check(
                [&](auto &oldValue) {
                  oldValue = value;
                  return true;
                },
                sequence);
          }
        }
        sqlite3_finalize(qry);
        BMessage result;
        for (const auto &[author, node] : graph) {
          BMessage mNode;
          for (const auto &[contact, edge] : node) {
            BMessage mEdge;
            std::pair<const char *, Updatable<bool>> properties[] = {
                {"following", edge.following},
                {"blocking", edge.blocking},
                {"pub", edge.pub}};
            for (const auto &[property, data] : properties) {
              if (data.threshold() >= 0) {
                BMessage mData;
                mData.AddBool("value", data.peek());
                mData.AddInt64("sequence", data.threshold());
                mEdge.AddMessage(property, &mData);
              }
            }
            mNode.AddMessage(contact, &mEdge);
          }
          result.AddMessage(author, &mNode);
        }
        reply.AddMessage("result", &result);
        error = B_OK;
      } break;
      case B_SET_PROPERTY: {
        BString name;
        if (specifier.FindString("name", &name) != B_OK)
          break;
        int32 delimiter = name.FindFirst(':');
        if (delimiter < 0)
          break;
        BString author, contact;
        name.CopyInto(author, 0, delimiter);
        name.CopyInto(contact, delimiter + 1, name.Length());
        if (BMessage data; message->FindMessage("data", &data) == B_OK) {
          error = B_OK;
          static const char *properties[] = {"following", "blocking", "pub"};
          for (const char *property : properties) {
            if (BMessage state; data.FindMessage(property, &state) == B_OK) {
              int64 sequence;
              bool value;
              if (state.FindInt64("sequence", &sequence) == B_OK &&
                  state.FindBool("value", &value) == B_OK) {
                // If we ever switch to using multiple database threads then
                // these will need to be in a transaction.
                sqlite3_stmt *qry;
                sqlite3_prepare_v2(this->database,
                                   "SELECT 1 FROM contacts "
                                   "WHERE author = ? "
                                   "AND contact = ? "
                                   "AND property = ?",
                                   -1, &qry, NULL);
                sqlite3_bind_text(qry, 1, author.String(), author.Length(),
                                  SQLITE_STATIC);
                sqlite3_bind_text(qry, 2, contact.String(), contact.Length(),
                                  SQLITE_STATIC);
                sqlite3_bind_text(qry, 3, property, -1, SQLITE_STATIC);
                int status = sqlite3_step(qry);
                sqlite3_finalize(qry);
                switch (status) {
                case SQLITE_ROW: {
                  status = sqlite3_prepare_v2(
                      this->database,
                      "UPDATE contacts SET sequence = ?, value = ? "
                      "WHERE author = ? "
                      "AND contact = ? "
                      "AND property = ?",
                      -1, &qry, NULL);
                  sqlite3_bind_int64(qry, 1, sequence);
                  sqlite3_bind_int64(qry, 2, value ? 1 : 0);
                  sqlite3_bind_text(qry, 3, author.String(), author.Length(),
                                    SQLITE_STATIC);
                  sqlite3_bind_text(qry, 4, contact.String(), contact.Length(),
                                    SQLITE_STATIC);
                  sqlite3_bind_text(qry, 5, property, -1, SQLITE_STATIC);
                  status = sqlite3_step(qry);
                  sqlite3_finalize(qry);
                } break;
                case SQLITE_DONE: {
                  status = sqlite3_prepare_v2(
                      this->database,
                      "INSERT INTO contacts("
                      "author, contact, property, sequence, value) "
                      "VALUES(?,?,?,?,?)",
                      -1, &qry, NULL);
                  sqlite3_bind_text(qry, 1, author.String(), author.Length(),
                                    SQLITE_STATIC);
                  sqlite3_bind_text(qry, 2, contact.String(), contact.Length(),
                                    SQLITE_STATIC);
                  sqlite3_bind_text(qry, 3, property, -1, SQLITE_STATIC);
                  sqlite3_bind_int64(qry, 4, sequence);
                  sqlite3_bind_int64(qry, 5, value ? 1 : 0);
                  status = sqlite3_step(qry);
                  sqlite3_finalize(qry);
                } break;
                default:
                  error = B_IO_ERROR;
                }
                if (status != SQLITE_DONE && status != SQLITE_ROW)
                  error = B_IO_ERROR;
              }
            }
          }
        }
      } break;
      }
    } break;
    default:
      return BHandler::MessageReceived(message);
    }
    reply.AddInt32("error", error);
    reply.AddString("message", strerror(error));
    if (message->ReturnAddress().IsValid())
      message->SendReply(&reply);
    return;
  } else {
    BHandler::MessageReceived(message);
  }
}

status_t ContactStore::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat+contactstore");
  BPropertyInfo propertyInfo(properties);
  data->AddFlat("messages", &propertyInfo);
  return BHandler::GetSupportedSuites(data);
}

BHandler *ContactStore::ResolveSpecifier(BMessage *msg, int32 index,
                                         BMessage *specifier, int32 what,
                                         const char *property) {
  return this;
}
