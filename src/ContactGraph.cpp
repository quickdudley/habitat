#include "BJSON.h"
#include "ContactGraph.h"
#include "Post.h"
#include "SignJSON.h"
#include <Looper.h>
#include <Query.h>

ContactSelection &ContactSelection::operator+=(const ContactSelection &other) {
#define MERGE_ONE(prop)                                                        \
  if (other.prop.size() > 0)                                                   \
  this->prop.insert(other.prop.begin(), other.prop.end())
  MERGE_ONE(selected);
  MERGE_ONE(blocked);
  MERGE_ONE(own);
#undef MERGE_ONE
  return *this;
}

ContactSelection
ContactSelection::operator+(const ContactSelection &other) const {
  ContactSelection result;
  result += *this;
  result += other;
  return result;
}

std::set<BString> ContactSelection::combine() const {
  std::set<BString> result;
  for (auto item : this->selected)
    result.insert(item);
  for (auto item : this->blocked)
    result.erase(item);
  for (auto item : this->own)
    result.insert(item);
  return result;
}

ContactLinkState::ContactLinkState()
    :
    following(false),
    blocking(false),
    pub(false) {}

ContactGraph::ContactGraph(BMessenger db, BMessenger store)
    :
    db(db),
    store(store) {}

void ContactGraph::MessageReceived(BMessage *message) {
  switch (message->what) {
  case B_GET_PROPERTY:
    this->sendState(message);
    break;
  case 'JSOB':
    return logContact(message);
  case 'DONE':
    break;
  case B_REPLY:
    if (BMessage result; message->FindMessage("result", &result) == B_OK) {
      status_t err;
      char *author;
      type_code attrtype;
      int32 aIndex = 0;
      while ((err = result.GetInfo(B_MESSAGE_TYPE, aIndex, &author,
                                   &attrtype)) != B_BAD_INDEX) {
        if (err == B_OK) {
          if (BMessage mNode; result.FindMessage(author, &mNode) == B_OK) {
            this->graph.try_emplace(author,
                                    std::map<BString, ContactLinkState>());
            auto &node = this->graph.find(author)->second;
            char *contact;
            int32 cIndex = 0;
            while ((err = mNode.GetInfo(B_MESSAGE_TYPE, cIndex, &contact,
                                        &attrtype)) != B_BAD_INDEX) {
              if (err == B_OK) {
                if (BMessage mEdge;
                    mNode.FindMessage(contact, &mEdge) == B_OK) {
                  node.try_emplace(contact, ContactLinkState());
                  auto &edge = node.find(contact)->second;
                  std::pair<const char *, Updatable<bool> *> properties[] = {
                      {"following", &edge.following},
                      {"blocking", &edge.blocking},
                      {"pub", &edge.pub}};
                  for (auto &[property, data] : properties) {
                    int64 sequence;
                    bool value;
                    if (BMessage mData;
                        mEdge.FindMessage(property, &mData) == B_OK &&
                        mData.FindInt64("sequence", &sequence) == B_OK &&
                        mData.FindBool("value", &value) == B_OK) {
                      data->check([&](auto &oldValue) { return value; },
                                  sequence);
                    }
                  }
                }
              }
            }
          }
        }
      }
      this->loaded = true;
      this->SendNotices('CTAC');
    }
    break;
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
  this->graph.try_emplace(author, std::map<BString, ContactLinkState>());
  auto &node = this->graph.find(author)->second;
  node.try_emplace(contact, ContactLinkState());
  auto &edge = node.find(contact)->second;
  bool changed = false;
  BMessage data;
  // TODO: DRY
  edge.following.check(
      [&](auto &oldValue) {
        bool value = oldValue;
        if (content.FindBool("following", &value) == B_OK) {
          changed = true;
          oldValue = value;
          {
            BMessage prop;
            prop.AddBool("value", value);
            prop.AddInt64("sequence", sequence);
            data.AddMessage("following", &prop);
          }
          return true;
        } else {
          return false;
        }
      },
      sequence);
  edge.blocking.check(
      [&](auto &oldValue) {
        bool value = oldValue;
        if (content.FindBool("blocking", &value) == B_OK) {
          changed = true;
          oldValue = value;
          {
            BMessage prop;
            prop.AddBool("value", value);
            prop.AddInt64("sequence", sequence);
            data.AddMessage("blocking", &prop);
          }
          return true;
        } else {
          return false;
        }
      },
      sequence);
  edge.pub.check(
      [&](auto &oldValue) {
        bool value = oldValue;
        if (content.FindBool("pub", &value) == B_OK) {
          changed = true;
          oldValue = value;
          {
            BMessage prop;
            prop.AddBool("value", value);
            prop.AddInt64("sequence", sequence);
            data.AddMessage("pub", &prop);
          }
          return true;
        } else {
          return false;
        }
      },
      sequence);
  if (changed) {

    BMessage setter(B_SET_PROPERTY);
    setter.AddMessage("data", &data);
    BString linkName(author);
    linkName << ":";
    linkName << contact;
    setter.AddSpecifier("Contact", linkName);
    BMessage reply;
    status_t err;
    if (this->store.SendMessage(&setter, &reply) == B_OK &&
        (reply.FindInt32("error", &err) != B_OK || err == B_OK)) {
      BMessage setter2(B_SET_PROPERTY);
      BMessage data2;
      data2.AddBool("processed", true);
      setter2.AddMessage("data", &data2);
      unsigned char msgHash[crypto_hash_sha256_BYTES];
      {
        JSON::RootSink rootSink(std::make_unique<JSON::Hash>(msgHash));
        JSON::fromBMessage(&rootSink, message);
      }
      setter2.AddSpecifier("Post", messageCypherkey(msgHash));
      this->db.SendMessage(&setter2);
    }
    if (this->loaded)
      this->SendNotices('CTAC');
  }
}

void ContactGraph::sendState(BMessage *request) {
  BMessage reply(B_REPLY);
  BMessage result;
  for (const auto &[node, edges] : this->graph) {
    BMessage branch;
    for (const auto &[subject, edge] : edges) {
      BMessage leaf;
      leaf.AddBool("following", edge.following.peek());
      leaf.AddBool("blocked", edge.blocking.peek());
      leaf.AddBool("pub", edge.pub.peek());
      branch.AddMessage(subject, &leaf);
    }
    result.AddMessage(node, &branch);
  }
  reply.AddMessage("result", &result);
  reply.AddInt32("error", B_OK);
  request->SendReply(&reply);
}
