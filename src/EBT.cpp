#include "EBT.h"
#include "Logging.h"
#include <MessageRunner.h>
#include <iostream>
#include <iterator>

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
    : note(decodeNote(note)),
      updated(system_time()) {}

RemoteState::RemoteState(const RemoteState &original)
    : note(original.note),
      updated(original.updated) {}

static Note compose(const LocalState &state, const LinkLocalState &linkState) {
  return {linkState.replicate, linkState.receive && !state.forked,
          state.sequence, state.savedSequence};
}

Dispatcher::Dispatcher(SSBDatabase *db)
    : BLooper("EBT"),
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
  // TODO: Break this up into smaller methods.
  if (msg->what == B_OBSERVER_NOTICE_CHANGE) {
    this->noticeChange(msg);
    return;
  } else if (msg->what == 'CLOG') {
    bool nowClogged;
    if (msg->FindBool("clogged", &nowClogged) != B_OK ||
        nowClogged == this->clogged) {
      return;
    }
    this->clogged = nowClogged;
    bool toggled;
    for (int i = 0; i < this->CountHandlers(); i++) {
      if (auto link = dynamic_cast<Link *>(this->HandlerAt(i))) {
        for (auto &state : link->ourState) {
          if (state.second.receive) {
            link->sendSequence.push(state.first);
            toggled = true;
          }
        }
      }
    }
    if (toggled)
      this->startNotesTimer(0);
  }
  if (msg->IsReply()) {
    if (status_t response; msg->FindInt32("error", &response) == B_OK &&
        (response == B_ENTRY_NOT_FOUND || response == B_NAME_NOT_FOUND)) {
      const BMessage *request = msg->Previous();
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
    bigtime_t staleThreshold = system_time() - 5000000;
    for (int i = this->CountHandlers() - 1; i >= 0; i--) {
      if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
        if (auto line = link->remoteState.find(cypherkey);
            line != link->remoteState.end()) {
          if (bestSoFar) {
            auto bestLine = bestSoFar->remoteState.find(cypherkey);
#define TIME_FORMULA(item)                                                     \
  staleThreshold + abs(staleThreshold - item->second.updated) -                \
      (item->second.note.receive ? 1000000 : 0)
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
    this->sendNotes();
    return;
  }
  {
    BMessage result;
    for (int32 i = 0; msg->FindMessage("result", i, &result) == B_OK; i++) {
      BString author;
      JSON::number sequence;
      if (result.FindDouble("sequence", &sequence) == B_OK &&
          result.FindString("author", &author) == B_OK) {
        for (int i = this->CountHandlers() - 1; i >= 0; i--)
          if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link)
            link->pushOut(&result);
      }
    }
  }
}

void Dispatcher::Quit() {
  this->StopWatchingAll(BMessenger(this->db));
  return BLooper::Quit();
}

void Dispatcher::initiate(muxrpc::Connection *connection) {
  BMessage argsObject('JSOB');
  argsObject.AddDouble("version", 3);
  argsObject.AddString("format", "classic");
  BMessage args('JSAR');
  args.AddMessage("0", &argsObject);
  Link *link = new Link(BMessenger(), true);
  if (this->Lock()) {
    this->AddHandler(link);
    if (connection->request({"ebt", "replicate"}, muxrpc::RequestType::DUPLEX,
                            &args, BMessenger(link),
                            link->outbound()) != B_OK) {
      delete link;
    }
    link->loadState();
    this->Unlock();
  }
}

void Dispatcher::noticeChange(BMessage *msg) {
  BString cypherkey;
  int64 sequence;
  if (msg->FindString("feed", &cypherkey) == B_OK &&
      msg->FindInt64("sequence", &sequence) == B_OK) {
    {
      BString logText("Observer notice for ");
      logText << cypherkey;
      logText << ": sequence = " << sequence;
      writeLog('EBT_', logText);
    }
    bool changed = false;
    bool justOne = !this->polyLink();
    bool forked = msg->GetBool("forked", false);
    bool fixup = msg->GetBool("broken", false);
    if (auto state = this->ourState.find(cypherkey);
        state != this->ourState.end()) {
      if (state->second.savedSequence != sequence) {
        changed = true;
        state->second.savedSequence = sequence;
      }
      if (fixup || state->second.sequence < sequence) {
        changed = true;
        state->second.sequence = sequence;
      }
      if (forked != state->second.forked) {
        changed = true;
        state->second.forked = forked;
      }
    } else {
      this->ourState.insert(
          {cypherkey, {(uint64)sequence, (uint64)sequence, forked}});
    }
    if (changed) {
      for (int32 i = this->CountHandlers() - 1; i >= 0; i--)
        if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link)
          link->sendSequence.push(cypherkey);
      this->startNotesTimer(1000);
    }
  } else if (BString cypherkey; msg->GetBool("deleted", false) &&
             msg->FindString("feed", &cypherkey) == B_OK) {
    this->ourState.erase(cypherkey);
    for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
      if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
        link->ourState.erase(cypherkey);
        link->sendSequence.push(cypherkey);
      }
    }
    this->startNotesTimer(1000);
  }
}

void Dispatcher::checkForMessage(const BString &author, uint64 sequence) {
  if (auto s = this->ourState.find(author);
      s != this->ourState.end() && s->second.savedSequence >= sequence) {
    BMessage message(B_GET_PROPERTY);
    BMessage specifier(B_INDEX_SPECIFIER);
    specifier.AddInt32("index", (int32)sequence);
    specifier.AddUInt16("count", 16);
    specifier.AddString("property", "Post");
    message.AddSpecifier(&specifier);
    message.AddSpecifier("ReplicatedFeed", author);
    BMessenger(this->db).SendMessage(&message, BMessenger(this));
  }
}

bool Dispatcher::polyLink() {
  int count = 0;
  for (int32 i = this->CountHandlers(); i >= 0; i--)
    if (dynamic_cast<Link *>(this->HandlerAt(i)) && ++count >= 2)
      return true;
  return false;
}

void Dispatcher::sendNotes() {
  for (int i = this->CountHandlers() - 1; i >= 0; i--) {
    if (Link *link = dynamic_cast<Link *>(this->HandlerAt(i)); link) {
      if (link->waiting)
        continue;
      while (!link->sendSequence.empty()) {
        BMessage content;
        int counter = 618;
        bool nonempty = false;
        while (counter > 0 && !link->sendSequence.empty()) {
          auto &feedID = link->sendSequence.front();
          int64 noteValue;
          if (auto state = this->ourState.find(feedID);
              state != this->ourState.end()) {
            if (auto linkState = link->ourState.find(feedID);
                linkState != link->ourState.end()) {
              auto noteStruct = compose(state->second, linkState->second);
              if (this->clogged)
                noteStruct.receive = false;
              noteValue = encodeNote(noteStruct);
            }
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
      }
    }
  }
  this->buildingNotes = false;
}

Begin::Begin(Dispatcher *dispatcher)
    : dispatcher(dispatcher) {
  this->expectedType = muxrpc::RequestType::DUPLEX;
  this->name.push_back("ebt");
  this->name.push_back("replicate");
}

Begin::~Begin() {}

Link::Link(muxrpc::Sender sender, bool waiting)
    : sender(sender),
      waiting(waiting) {}

void Link::MessageReceived(BMessage *message) {
  if (message->what == 'SENT') {
    this->sendOne();
  } else if (BMessage content;
             message->FindMessage("content", &content) == B_OK) {
    BString author;
    if (content.FindString("author", &author) == B_OK) {
      this->stopWaiting();
      if (auto dsp = dynamic_cast<Dispatcher *>(this->Looper());
          dsp != NULL && dsp->clogged) {
        writeLog('RMCD', "Receiving messages while clogged!");
        dsp->sendNotes();
      } else {
        this->tick(author);
        int64 sequence = (int64)message->GetDouble("sequence", 0.0);
        BMessenger(this->db()).SendMessage(&content);
        if (auto dispatcher = dynamic_cast<Dispatcher *>(this->Looper())) {
          if (auto state = dispatcher->ourState.find(author);
              state != dispatcher->ourState.end() &&
              sequence == state->second.sequence + 1) {
            state->second.sequence = sequence;
          }
        }
      }
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
            this->stopWaiting();
            const auto &inserted = this->remoteState.insert_or_assign(
                BString(attrname), RemoteState(note));
            Dispatcher *dispatcher = dynamic_cast<Dispatcher *>(this->Looper());
            if (this->ourState.find(attrname) != this->ourState.end()) {
              if (inserted.first->second.note.receive) {
                auto q = this->outMessages.find(attrname);
                if (q != this->outMessages.end()) {
                  while (!q->second.empty() &&
                         q->second.front().GetDouble("sequence", 0.0) !=
                             inserted.first->second.note.sequence + 1) {
                    q->second.pop();
                  }
                  if (q->second.empty()) {
                    this->outMessages.erase(q);
                    q = this->outMessages.end();
                  }
                }
                if (q == this->outMessages.end()) {
                  dispatcher->checkForMessage(
                      inserted.first->first,
                      inserted.first->second.note.sequence + 1);
                }
              } else {
                this->outMessages.erase(attrname);
              }
              this->tick(attrname);
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
      if (content.FindString("cypherkey", &feedId) == B_OK) {
        this->ourState.insert({feedId, {true, shouldReplicate}});
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
    BMessage checkLatency('CKSR');
    checkLatency.AddString("cypherkey", author);
    BMessenger(this->Looper()).SendMessage(&checkLatency);
  } else {
    auto [entry, inserted] = this->lastSent.insert({author, INT64_MIN});
    if (inserted || entry->second != INT64_MIN) {
      entry->second = INT64_MIN;
      this->sendSequence.push(author);
      dynamic_cast<Dispatcher *>(this->Looper())->startNotesTimer(1000);
    }
  }
}

void Link::stopWaiting() {
  if (this->waiting) {
    if (auto dispatcher = dynamic_cast<Dispatcher *>(this->Looper());
        dispatcher != NULL) {
      this->waiting = false;
      dispatcher->startNotesTimer(1000);
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

BMessenger *Link::outbound() { return this->sender.outbound(); }

void Link::pushOut(BMessage *message) {
  BString author;
  JSON::number sequence;
  if (message->FindDouble("sequence", &sequence) == B_OK &&
      message->FindString("author", &author) == B_OK) {
    if (auto state = this->remoteState.find(author);
        state != this->remoteState.end()) {
      double oldSequence = state->second.note.sequence;
      auto q = this->outMessages.find(author);
      if (q != this->outMessages.end() && !q->second.empty())
        oldSequence = q->second.back().GetDouble("sequence", oldSequence);
      if (oldSequence + 1 == sequence) {
        if (q != this->outMessages.end())
          q->second.push(*message);
        else
          this->outMessages[author].push(*message);
        if (!this->sending) {
          this->sending = true;
          this->sendOne();
        }
        // TODO: Use different numbers for queued and sent
      }
    }
  }
}

void Link::sendOne() {
  while (true) {
    if (this->outMessages.empty()) {
      this->sending = false;
      return;
    }
    size_t index =
        std::uniform_int_distribution<size_t>(0, this->outMessages.size() - 1)(
            static_cast<Dispatcher *>(this->Looper())->rng);
    auto q = index > this->outMessages.size() / 2
        ? std::prev(this->outMessages.end(), this->outMessages.size() - index)
        : std::next(this->outMessages.begin(), index);
    const BString author = q->first;
    if (auto state = this->remoteState.find(author);
        state != this->remoteState.end()) {
      if (!state->second.note.receive) {
        this->outMessages.erase(q);
        continue;
      }
      bool wasEmpty = true;
      while (!q->second.empty() &&
             q->second.front().GetDouble("sequence", 0) !=
                 state->second.note.sequence + 1) {
        q->second.pop();
        wasEmpty = false;
      }
      if (q->second.empty()) {
        this->outMessages.erase(q);
        if (!wasEmpty) {
          static_cast<Dispatcher *>(this->Looper())
              ->checkForMessage(author,
                                (uint64)state->second.note.sequence + 1);
        }
        continue;
      }
      this->sender.send(&q->second.front(), true, false, false,
                        BMessenger(this));
      state->second.note.sequence++;
      q->second.pop();
      if (q->second.empty()) {
        this->outMessages.erase(q);
        static_cast<Dispatcher *>(this->Looper())
            ->checkForMessage(author, (uint64)state->second.note.sequence + 1);
      }
      break;
    } else {
      this->outMessages.erase(q);
    }
  }
}

void Dispatcher::startNotesTimer(bigtime_t delay) {
  if (this->buildingNotes == false) {
    BMessage timerMsg('SDNT');
    if (delay > 0)
      BMessageRunner::StartSending(BMessenger(this), &timerMsg, delay, 1);
    else
      BMessenger(this).SendMessage(&timerMsg);
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

void Begin::call(muxrpc::Connection *connection) {
  this->dispatcher->initiate(connection);
}
} // namespace ebt
