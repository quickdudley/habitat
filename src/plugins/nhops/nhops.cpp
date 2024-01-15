#include "ContactGraph.h"
#include <set>

extern "C" const char *pluginName() { return "n-hops"; }

extern "C" status_t selectContacts(ContactSelection *target,
                                   std::set<BString> *roots, BMessage *config,
                                   BMessage *graph) {
  int32 hops;
  if (status_t status = config->FindInt32("hops", &hops); status != B_OK)
    return status;
  std::set<BString> visited;
  std::set<BString> pending = *roots;
  target->own = *roots;
  for (const BString &root : *roots) {
    BMessage node;
    if (graph->FindMessage(root, &node) == B_OK) {
      status_t err;
      char *attrname;
      type_code attrtype;
      int32 index = 0;
      while ((err = node.GetInfo(B_MESSAGE_TYPE, index, &attrname,
                                 &attrtype)) != B_BAD_INDEX) {
        BMessage edge;
        if (err == B_OK && node.FindMessage(attrname, &edge) == B_OK &&
            edge.GetBool("blocked", false)) {
          visited.insert(attrname);
          target->blocked.insert(attrname);
        }
        index++;
      }
    }
  }
  for (int32 i = 0; i < hops; i++) {
    std::set<BString> layer = pending;
    pending.clear();
    for (const BString &key : layer) {
      BMessage node;
      if (graph->FindMessage(key, &node) == B_OK) {
        status_t err;
        char *attrname;
        type_code attrtype;
        int32 index = 0;
        while ((err = node.GetInfo(B_MESSAGE_TYPE, index, &attrname,
                                   &attrtype)) != B_BAD_INDEX) {
          BMessage edge;
          if (err == B_OK && visited.find(attrname) == visited.end() &&
              target->blocked.find(attrname) == target->blocked.end() &&
              node.FindMessage(attrname, &edge) == B_OK &&
              edge.GetBool("following", true) &&
              !edge.GetBool("blocked", false)) {
            pending.insert(attrname);
            target->selected.insert(attrname);
          }
        }
      }
    }
  }
  return B_OK;
}

extern "C" status_t defaultConfig(BMessage *target) {
  return target->AddInt32("hops", 2);
}
