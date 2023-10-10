#include "EBT.h"
#include <MessageRunner.h>

namespace ebt {

Note decodeNote(double note) {
  bool replicate = note >= 0;
  uint64 v = note;
  bool receive = replicate && (v & 1) == 0;
  return {replicate, receive, replicate ? v >> 1 : 0};
}

double encodeNote(Note &note) {
  if (!note.replicate)
    return -1;
  return (note.sequence << 1) | (note.receive ? 0 : 1);
}

RemoteState::RemoteState(double note)
    :
    note(decodeNote(note)),
    updated(std::time(NULL)) {}

RemoteState::RemoteState(const RemoteState &original)
    :
    note(original.note),
    updated(original.updated) {}

Dispatcher::Dispatcher(SSBDatabase *db)
    :
    db(db) {}

status_t Dispatcher::GetSupportedSuites(BMessage *data) {
  // TODO
  return BLooper::GetSupportedSuites(data);
}

BHandler *Dispatcher::ResolveSpecifier(BMessage *msg, int32 index,
                                       BMessage *specifier, int32 what,
                                       const char *property) {
  // TODO
  return BLooper::ResolveSpecifier(msg, index, specifier, what, property);
}

void Dispatcher::MessageReceived(BMessage *msg) {
  if (msg->what == B_OBSERVER_NOTICE_CHANGE) {
    BString cypherkey;
    int64 sequence;
    if (msg->FindString("feed", &cypherkey) == B_OK &&
        msg->FindInt64("sequence", &sequence)) {
      bool changed = false;
      for (int32 i = this->CountHandlers() - 1; i >= 0; i++) {
        if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
          if (auto foundNote = link->ourState.find(cypherkey);
              foundNote != link->ourState.end()) {
            if (foundNote->second.sequence != sequence) {
              foundNote->second.sequence = sequence;
              changed = true;
              link->unsent.insert(cypherkey);
              link->sendSequence.push(cypherkey);
            }
          } else {
            link->ourState.insert({cypherkey, {true, false, (uint64)sequence}});
            changed = true;
            link->unsent.insert(cypherkey);
            link->sendSequence.push(cypherkey);
          }
        }
      }
      if (changed)
        this->startNotesTimer(1000);
    }
    return;
  }
  if (msg->IsReply()) {
    if (status_t response;
        msg->FindInt32("error", &response) == B_OK &&
        (response == B_ENTRY_NOT_FOUND || response == B_NAME_NOT_FOUND)) {
      BMessage request = *msg->Previous();
      int32 index;
      BMessage specifier;
      const char *property;
      BString feedId;
      BString cypherkey;
      bool gotSequence = false;
      for (int32 i = 0; request.SetCurrentSpecifier(i) == B_OK; i++) {
        if (request.GetCurrentSpecifier(&index, &specifier, NULL, &property) ==
                B_OK &&
            BString(property) == "ReplicatedFeed") {
          BString feedId;
          if (specifier.FindString("name", &feedId) == B_OK) {
            cypherkey = feedId;
          }
        } else if (BString(property) == "Sequence")
          gotSequence = true;
      }
      if (gotSequence) {
        BMessage timerMsg('CKSR');
        timerMsg.AddString("cypherkey", feedId);
        BMessageRunner::StartSending(BMessenger(this), &timerMsg, 1000000, 1);
        return;
      } else if (cypherkey != "") {
        for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
          if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
            link->unsent.insert(cypherkey);
            link->sendSequence.push(cypherkey);
            this->startNotesTimer(1000);
            return;
          }
        }
      }
    }
  }
  if (BString cypherkey;
      msg->what == 'CKSR' && msg->FindString("cypherkey", &cypherkey) == B_OK) {
    Link *bestSoFar = NULL;
    bool anyChanged = false;
    for (int i = this->CountHandlers() - 1; i >= 0; i--) {
      if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
        if (auto line = link->remoteState.find(cypherkey);
            line != link->remoteState.end()) {
          if (bestSoFar) {
            auto bestLine = bestSoFar->remoteState.find(cypherkey);
            if (line->second.note.sequence > bestLine->second.note.sequence ||
                (line->second.note.sequence == bestLine->second.note.sequence &&
                 line->second.updated < bestLine->second.updated))
              bestSoFar = link;
          } else
            bestSoFar = link;
        }
      }
    }
    for (int i = this->CountHandlers() - 1; i >= 0; i--) {
      if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
        bool receiving = link == bestSoFar;
        if (auto line = link->remoteState.find(cypherkey);
            line != link->remoteState.end()) {
          if (line->second.note.receive != receiving) {
            anyChanged = true;
            line->second.note.receive = receiving;
            link->unsent.insert(cypherkey);
            link->sendSequence.push(cypherkey);
          }
        }
      }
    }
    if (anyChanged)
      this->startNotesTimer(1000);
  }
  if (msg->what == 'SDNT') {
    bool anyRemaining = false;
    for (int i = this->CountHandlers() - 1; i >= 0; i--) {
      if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
        BMessage content;
        int counter = 20;
        bool nonempty;
        while (counter > 0 && !link->sendSequence.empty()) {
          if (link->unsent.erase(link->sendSequence.front()) > 0) {
            if (auto state = link->ourState.find(link->sendSequence.front());
                state != link->ourState.end()) {
              nonempty = true;
              counter--;
              content.AddDouble(link->sendSequence.front(),
                                encodeNote(state->second));
            }
          }
          link->sendSequence.pop();
        }
        if (nonempty) {
          link->sender.send(&content, true, false, false);
        }
        if (!link->sendSequence.empty())
          anyRemaining = true;
      }
    }
    if (anyRemaining)
      BMessenger(this).SendMessage(msg);
    return;
  }
  {
    BMessage result;
    if (msg->FindMessage("result", &result) == B_OK) {
      BString author;
      JSON::number sequence;
      if (result.FindDouble("sequence", &sequence) == B_OK &&
          result.FindString("author", &author) == B_OK) {
        uint64 actualSequence = sequence;
        bool sentAny = false;
        for (int i = this->CountHandlers() - 1; i >= 0; i--) {
          if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
            if (auto state = link->remoteState.find(author);
                state != link->remoteState.end()) {
              if (state->second.note.sequence + 1 == sequence) {
                link->sender.send(&result, true, false, false);
                state->second.note.sequence++;
                sentAny = true;
              }
            }
          }
        }
        if (sentAny) {
          this->checkForMessage(author, actualSequence + 1);
        }
        return;
      }
    }
  }
  return BLooper::MessageReceived(msg);
}

void Dispatcher::checkForMessage(const BString &author, uint64 sequence) {
  BMessage message(B_GET_PROPERTY);
  message.AddSpecifier("ReplicatedFeed", author);
  message.AddSpecifier("Post", (int32)sequence);
  BMessenger(this->db).SendMessage(&message, BMessenger(this));
}

Begin::Begin(Dispatcher *dispatcher)
    :
    dispatcher(dispatcher) {
  this->expectedType = muxrpc::RequestType::DUPLEX;
  this->name.push_back("ebt");
  this->name.push_back("replicate");
}

Begin::~Begin() {}

Link::Link(muxrpc::Sender sender)
    :
    sender(sender) {}

void Link::MessageReceived(BMessage *message) {
  BMessage content;
  if (message->FindMessage("content", &content) == B_OK) {
    BString author;
    if (content.FindString("author", &author) == B_OK) {
      BMessenger(this->db()).SendMessage(message);
    } else {
      status_t err;
      char *attrname;
      type_code attrtype;
      int32 index = 0;
      while ((err = content.GetInfo(B_DOUBLE_TYPE, index, &attrname,
                                    &attrtype)) != B_BAD_INDEX) {
        if (err == B_OK) {
          double note;
          if (content.FindDouble(attrname, &note)) {
            const auto &inserted = this->remoteState.insert_or_assign(
                BString(attrname), RemoteState(note));
            Dispatcher *dispatcher = dynamic_cast<Dispatcher *>(this->Looper());
            dispatcher->checkForMessage(inserted.first->first,
                                        inserted.first->second.note.sequence);
            if (inserted.first->second.note.receive)
              dispatcher->checkForMessage(inserted.first->first,
                                          inserted.first->second.note.sequence +
                                              1);
          }
        }
      }
    }
  } else if (message->FindMessage("result", &content) == B_OK) {
    for (int32 index = 0;
        message->FindMessage("result", index, &content) == B_OK; index++) {
      BString feedId;
      uint64 sequence;
      if (content.FindUInt64("sequence", &sequence) == B_OK &&
          content.FindString("cypherkey", &feedId)) {
        this->ourState.insert({feedId, {true, false, sequence}});
        this->unsent.emplace(feedId);
      }
    }
    if (!this->unsent.empty())
      dynamic_cast<Dispatcher *>(this->Looper())->startNotesTimer(1000);
  }
  // TODO: Check for end-of-stream flags
}

void Dispatcher::startNotesTimer(bigtime_t delay) {
  if (this->buildingNotes == false) {
    BMessage timerMsg('SDNT');
    BMessageRunner::StartSending(BMessenger(this), &timerMsg, delay, 1);
    this->buildingNotes = true;
  }
}

SSBDatabase *Link::db() {
  Dispatcher *dispatcher = dynamic_cast<Dispatcher *>(this->Looper());
  if (dispatcher != NULL)
    return dispatcher->db;
  else
    return NULL;
}

} // namespace ebt