#include "EBT.h"
#include "Logging.h"
#include <MessageRunner.h>
#include <iostream>

namespace ebt {

Note decodeNote(double note) {
  bool replicate = note >= 0;
  uint64 v = note;
  bool receive = replicate && (v & 1) == 0;
  uint64 sequence = replicate ? v >> 1 : 0;
  return {replicate, receive, sequence, sequence};
}

int64 encodeNote(Note &note) {
  if (!note.replicate)
    return -1;
  return note.receive ? note.sequence << 1 : (note.savedSequence << 1) | 1;
}

RemoteState::RemoteState(double note)
    :
    note(decodeNote(note)),
    updated(system_time()) {}

RemoteState::RemoteState(const RemoteState &original)
    :
    note(original.note),
    updated(original.updated) {}

Dispatcher::Dispatcher(SSBDatabase *db)
    :
    BLooper("EBT"),
    db(db) {}

thread_id Dispatcher::Run() {
  thread_id thread = BLooper::Run();
  if (thread > 0) {
    BMessage sub('USUB');
    sub.AddSpecifier("ReplicatedFeed");
    sub.AddMessenger("subscriber", BMessenger(this));
    BMessenger(this->db).SendMessage(&sub);
  }
  return thread;
}

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
    int64 saved;
    if (msg->FindString("feed", &cypherkey) == B_OK &&
        msg->FindInt64("sequence", &sequence) == B_OK &&
        msg->FindInt64("saved", &saved) == B_OK) {
      {
        BString logText("Observer notice for ");
        logText << cypherkey;
        logText << ": sequence = " << sequence;
        writeLog('EBT_', logText);
      }
      bool changed = false;
      bool justOne = !this->polyLink();
      for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
        if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
          if (auto foundNote = link->ourState.find(cypherkey);
              foundNote != link->ourState.end()) {
            if (foundNote->second.sequence != sequence) {
              foundNote->second.sequence = sequence;
              changed = true;
              link->sendSequence.push(cypherkey);
            }
          } else {
            link->ourState.insert(
                {cypherkey, {true, justOne, (uint64)sequence, (uint64)saved}});
            changed = true;
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
    if (status_t response; msg->FindInt32("error", &response) == B_OK &&
        (response == B_ENTRY_NOT_FOUND || response == B_NAME_NOT_FOUND)) {
      const BMessage *request = msg->Previous();
      int32 index;
      BMessage specifier;
      BString property;
      BString feedId;
      BString cypherkey;
      bool gotSequence = false;
      for (int32 i = 0;
        request->FindMessage("specifiers", i, &specifier) == B_OK; i++) {
        if (specifier.FindString("property", &property) == B_OK) {
          if (property == "ReplicatedFeed") {
            BString feedId;
            if (specifier.FindString("name", &feedId) == B_OK)
              cypherkey = feedId;
          } else if (int32 sequence; property == "Post" &&
                     specifier.FindInt32("index", &sequence) == B_OK) {
            gotSequence = true;
          }
        }
      }
      if (response != B_NAME_NOT_FOUND && gotSequence && cypherkey != "") {
        BMessage timerMsg('CKSR');
        timerMsg.AddString("cypherkey", cypherkey);
        BMessageRunner::StartSending(BMessenger(this), &timerMsg, 1000000, 1);
        return;
      } else if (cypherkey != "") {
        for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
          if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
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
    bigtime_t now = system_time();
    for (int i = this->CountHandlers() - 1; i >= 0; i--) {
      if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
        if (auto line = link->remoteState.find(cypherkey);
            line != link->remoteState.end()) {
          if (bestSoFar) {
            auto bestLine = bestSoFar->remoteState.find(cypherkey);
#define TIME_FORMULA(item)                                                     \
  abs(item->second.updated + item->second.note.receive ? 5000000 : 6000000)
            if (line->second.note.sequence > bestLine->second.note.sequence ||
                (line->second.note.sequence == bestLine->second.note.sequence &&
                 TIME_FORMULA(line) < TIME_FORMULA(bestLine))) {
              bestSoFar = link;
            }
          } else {
            bestSoFar = link;
          }
#undef TIME_FORMULA
        }
      }
    }
    for (int i = this->CountHandlers() - 1; i >= 0; i--) {
      if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
        bool receiving = link == bestSoFar;
        if (auto line = link->ourState.find(cypherkey);
            line != link->ourState.end()) {
          if (line->second.receive != receiving) {
            anyChanged = true;
            line->second.receive = receiving;
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
        int counter = 50;
        bool nonempty;
        while (counter > 0 && !link->sendSequence.empty()) {
          auto &feedID = link->sendSequence.front();
          int64 noteValue;
          if (auto state = link->ourState.find(feedID);
              state != link->ourState.end()) {
            noteValue = encodeNote(state->second);
          } else {
            noteValue = -1;
          }
          auto [sentValue, insertedSent] =
              link->lastSent.insert({feedID, noteValue});
          if (insertedSent || sentValue->second != noteValue) {
            nonempty = true;
            counter--;
            sentValue->second = noteValue;
            content.AddInt64(feedID, noteValue);
          }
          link->sendSequence.pop();
        }
        if (nonempty)
          link->sender.send(&content, true, false, false);
        if (!link->sendSequence.empty())
          anyRemaining = true;
      }
    }
    if (anyRemaining)
      BMessenger(this).SendMessage(msg);
    else
      this->buildingNotes = false;
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
        if (sentAny)
          this->checkForMessage(author, actualSequence + 1);
        return;
      }
    }
  }
  return BLooper::MessageReceived(msg);
}

void Dispatcher::Quit() {
  this->StopWatchingAll(BMessenger(this->db));
  return BLooper::Quit();
}

void Dispatcher::checkForMessage(const BString &author, uint64 sequence) {
  BMessage message(B_GET_PROPERTY);
  message.AddSpecifier("Post", (int32)sequence);
  message.AddSpecifier("ReplicatedFeed", author);
  BMessenger(this->db).SendMessage(&message, BMessenger(this));
}

bool Dispatcher::polyLink() {
  int count = 0;
  for (int32 i = this->CountHandlers(); i >= 0; i--) {
    if (dynamic_cast<Link *>(this->HandlerAt(i)) && ++count >= 2)
      return true;
  }
  return false;
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
      this->tick(author);
      BMessenger(this->db()).SendMessage(&content);
    } else {
      status_t err;
      char *attrname;
      type_code attrtype;
      int32 index = 0;
      while ((err = content.GetInfo(B_DOUBLE_TYPE, index, &attrname,
                                    &attrtype)) != B_BAD_INDEX) {
        if (err == B_OK) {
          double note;
          if (content.FindDouble(attrname, &note) == B_OK) {
            const auto &inserted = this->remoteState.insert_or_assign(
                BString(attrname), RemoteState(note));
            Dispatcher *dispatcher = dynamic_cast<Dispatcher *>(this->Looper());
            if (this->ourState.find(attrname) != this->ourState.end()) {
              dispatcher->checkForMessage(inserted.first->first,
                                          inserted.first->second.note.sequence);
              if (inserted.first->second.note.receive) {
                dispatcher->checkForMessage(
                    inserted.first->first,
                    inserted.first->second.note.sequence + 1);
              }
            } else {
              this->sendSequence.push(attrname);
              dynamic_cast<Dispatcher *>(this->Looper())->startNotesTimer(1000);
            }
          }
        }
        index++;
      }
    }
  } else {
    int32 i = 0;
    bool shouldReplicate =
        !dynamic_cast<Dispatcher *>(this->Looper())->polyLink();
    while (message->FindMessage("result", i, &content) == B_OK) {
      BString feedId;
      uint64 sequence;
      if (content.FindUInt64("sequence", &sequence) == B_OK &&
          content.FindString("cypherkey", &feedId) == B_OK) {
        this->ourState.insert({feedId, {true, shouldReplicate, sequence}});
        this->sendSequence.push(feedId);
      }
      i++;
    }
    dynamic_cast<Dispatcher *>(this->Looper())->startNotesTimer(1000);
  }
  if (!message->GetBool("stream", true) || message->GetBool("end", false)) {
    Dispatcher *dispatcher = dynamic_cast<Dispatcher *>(this->Looper());
    if (this->Looper())
      this->Looper()->RemoveHandler(this);
    if (dispatcher) {
      for (auto state : this->ourState) {
        if (state.second.receive) {
          BMessage refind('CKSR');
          refind.AddString("cypherkey", state.first);
          BMessenger(dispatcher).SendMessage(&refind);
        }
      }
    }
    delete this;
  }
}

void Link::tick(const BString &author) {
  if (auto line = this->remoteState.find(author);
      line != this->remoteState.end()) {
    line->second.updated = system_time();
  } else {
    auto [entry, inserted] = this->lastSent.insert({author, INT64_MIN});
    if (inserted || entry->second != INT64_MIN) {
      entry->second = INT64_MIN;
      this->sendSequence.push(author);
      dynamic_cast<Dispatcher *>(this->Looper())->startNotesTimer(1000);
    }
  }
}

void Link::loadState() {
  SSBDatabase *db = this->db();
  if (db) {
    BMessage request(B_GET_PROPERTY);
    request.AddSpecifier("ReplicatedFeed");
    BMessenger(db).SendMessage(&request, BMessenger(this));
  }
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

status_t Begin::call(muxrpc::Connection *connection, muxrpc::RequestType type,
                     BMessage *args, BMessenger replyTo, BMessenger *inbound) {
  BMessage argsObject;
  if (args->FindMessage("0", &argsObject) != B_OK)
    return B_ERROR;
  if (argsObject.GetDouble("version", 3) != 3)
    return B_ERROR;
  if (BString("classic") != argsObject.GetString("format", "classic"))
    return B_ERROR;
  Link *link = new Link(muxrpc::Sender(replyTo));
  this->dispatcher->Lock();
  this->dispatcher->AddHandler(link);
  *inbound = BMessenger(link);
  link->loadState();
  this->dispatcher->Unlock();
  return B_OK;
}
} // namespace ebt
