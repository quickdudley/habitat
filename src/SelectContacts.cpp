#include "SelectContacts.h"
#include "ContactGraph.h"
#include "Plugin.h"
#include <Application.h>
#include <map>

SelectContacts::SelectContacts(const BMessenger &db, const BMessenger &graph)
    :
    db(db),
    graph(graph) {}

void SelectContacts::MessageReceived(BMessage *message) {
  switch (message->what) {
  case B_REPLY: {
    message->Previous()->PrintToStream();
    BMessage prevSpec;
    BString property;
    if (message->Previous() &&
        message->Previous()->FindMessage("specifiers", &prevSpec) == B_OK &&
        prevSpec.FindString("property", &property) == B_OK &&
        property == "ReplicatedFeed") {
      BMessage inner;
      this->current.clear();
      for (int i = 0; message->FindMessage("result", i, &inner) == B_OK; i++) {
        if (BString cypherkey;
            inner.FindString("cypherkey", &cypherkey) == B_OK) {
          this->current.insert(cypherkey);
        }
      }
    } else {
      if (int32 status;
          message->FindInt32("error", &status) == B_OK && status == B_OK) {
        if (BMessage result; message->FindMessage("result", &result) == B_OK)
          this->makeSelection(&result);
      }
      this->fetching = false;
    }
  } break;
  case 'INIT': {
    this->StartWatching(this->graph, 'CTAC');
    BMessage get(B_GET_PROPERTY);
    get.AddSpecifier("ReplicatedFeed");
    BMessenger(be_app).SendMessage(&get, BMessenger(this));
  } break;
  case B_OBSERVER_NOTICE_CHANGE:
    if (!this->fetching) {
      this->fetching = true;
      BMessage rq(B_GET_PROPERTY);
      this->graph.SendMessage(&rq, BMessenger(this));
    }
    break;
  default:
    BHandler::MessageReceived(message);
    break;
  }
}

void SelectContacts::makeSelection(BMessage *graph) {
  std::set<BString> ownFeeds;
  {
    BMessage getOwnFeeds(B_GET_PROPERTY);
    getOwnFeeds.AddSpecifier("OwnFeed");
    BMessage response;
    if (this->db.SendMessage(&getOwnFeeds, &response) != B_OK)
      return;
    const char *result;
    for (int i = 0; response.FindString("result", i, &result) == B_OK; i++)
      ownFeeds.insert(result);
  }
  typedef status_t (*mkConfig_t)(BMessage *);
  std::map<BString, mkConfig_t> configurators;
  for (auto &[plugname, plugPtr] : habitat_plugins.lookup("defaultConfig")) {
    auto configurator = (mkConfig_t)plugPtr;
    if (configurator != NULL)
      configurators.insert({plugname, configurator});
  }
  ContactSelection full;
  for (auto &[plugname, plugPtr] : habitat_plugins.lookup("selectContacts")) {
    typedef status_t (*algorithm_t)(ContactSelection *, std::set<BString> *,
                                    BMessage *, BMessage *);
    auto algorithm = (algorithm_t)plugPtr;
    if (plugPtr == NULL)
      continue;
    ContactSelection single;
    BMessage config;
    if (auto configurator = configurators.find(plugname);
        configurator != configurators.end()) {
      (configurator->second)(&config);
    }
    if (algorithm(&single, &ownFeeds, &config, graph) == B_OK)
      full += single;
  }
  auto leftover = this->current;
  this->current = full.combine();
  for (auto item : this->current) {
    if (leftover.find(item) == leftover.end()) {
      BMessage create(B_CREATE_PROPERTY);
      BMessage reply;
      create.AddSpecifier("ReplicatedFeed");
      create.AddString("cypherkey", item);
      this->db.SendMessage(&create, &reply);
    } else {
      leftover.erase(item);
    }
  }
  for (auto item : leftover) {
    BMessage request(B_DELETE_PROPERTY);
    BMessage reply;
    request.AddSpecifier("ReplicatedFeed", item);
    this->db.SendMessage(&request, &reply);
  }
  this->db.SendMessage('GCOK');
}
