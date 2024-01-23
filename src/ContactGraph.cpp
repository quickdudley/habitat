#include "ContactGraph.h"
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

void ContactLinkState::archive(BMessage *message) const {
#define ARCHIVE_FIELD(field)                                                   \
  {                                                                            \
    BMessage pack;                                                             \
    pack.AddBool("value", this->field.peek());                                 \
    pack.AddInt64("sequence", this->field.threshold());                        \
    message->AddMessage(#field, &pack);                                        \
  }
  ARCHIVE_FIELD(following)
  ARCHIVE_FIELD(blocking)
  ARCHIVE_FIELD(pub)
#undef ARCHIVE_FIELD
}

void ContactLinkState::unarchive(BMessage *message) {
  BMessage pack;
#define UNARCHIVE_FIELD(field)                                                 \
  if (message->FindMessage(#field, &pack) == B_OK) {                           \
    int64 sequence;                                                            \
    bool value;                                                                \
    if (pack.FindInt64("sequence", &sequence) == B_OK &&                       \
        pack.FindBool("value", &value) == B_OK)                                \
      this->field.put(value, sequence);                                        \
  }
  UNARCHIVE_FIELD(following)
  UNARCHIVE_FIELD(blocking)
  UNARCHIVE_FIELD(pub)
#undef UNARCHIVE_FIELD
}

ContactGraph::ContactGraph(const BVolume &volume)
    :
    volume(volume) {
  BQuery query;
  query.SetVolume(&volume);
  query.PushAttr("HABITAT:cypherkey");
  query.PushString("@");
  query.PushOp(B_BEGINS_WITH);
  query.Fetch();
  entry_ref ref;
  while (query.GetNextRef(&ref) == B_OK) {
    BMessage metadata;
    BMessage contacts;
    BFile file(&ref, B_READ_ONLY);
    BString who;
    if (file.ReadAttrString("HABITAT:cypherkey", &who) != B_OK)
      continue;
    auto &node =
        this->graph.insert({who, std::map<BString, ContactLinkState>()})
            .first->second;
    if (metadata.Unflatten(&file) == B_OK &&
        metadata.FindMessage("contacts", &contacts) == B_OK) {
      status_t err;
      char *attrname;
      type_code attrtype;
      int32 index = 0;
      while ((err = contacts.GetInfo(B_MESSAGE_TYPE, index, &attrname,
                                     &attrtype)) != B_BAD_INDEX) {
        BMessage edge;
        if (contacts.FindMessage(attrname, &edge) == B_OK) {
          auto &sedge =
              node.insert({attrname, ContactLinkState()}).first->second;
          sedge.unarchive(&edge);
          index++;
        }
      }
    }
  }
}

void ContactGraph::MessageReceived(BMessage *message) {
  switch (message->what) {
  case B_GET_PROPERTY:
    this->sendState(message);
    break;
  case 'JSOB':
    return logContact(message);
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
  edge.following.check(
      [&](auto &oldValue) {
        bool value = oldValue;
        if (content.FindBool("following", &value) == B_OK) {
          changed = true;
          oldValue = value;
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
          return true;
        } else {
          return false;
        }
      },
      sequence);
  if (changed) {
    BMessage metadata;
    BMessage contacts;
    BQuery query;
    entry_ref entry;
    BFile metafile;
    query.SetVolume(&this->volume);
    query.PushAttr("HABITAT:cypherkey");
    query.PushString(author);
    query.PushOp(B_EQ);
    if (query.Fetch() != B_OK)
      goto notices;
    if (query.GetNextRef(&entry) != B_OK)
      goto notices;
    if (metafile.SetTo(&entry, B_READ_WRITE) != B_OK)
      goto notices;
    if (metadata.Unflatten(&metafile) != B_OK)
      goto notices;
    if (metafile.Seek(0, SEEK_SET) != B_OK)
      goto notices;
    if (metafile.SetSize(0) != B_OK)
      goto notices;
    metadata.RemoveName("contacts");
    for (const auto &[peer, status] : node) {
      BMessage subrecord;
      status.archive(&subrecord);
      contacts.AddMessage(peer, &subrecord);
    }
    metadata.AddMessage("contacts", &contacts);
    metadata.Flatten(&metafile);
  notices:
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
