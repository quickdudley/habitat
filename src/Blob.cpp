#include "Blob.h"
#include <NodeMonitor.h>
#include <algorithm>

namespace blob {

void Wanted::MessageReceived(BMessage *message) {
  switch(message->what) {
  	case B_QUERY_UPDATE: {
  	  if (message->GetInt32("opcode", B_ERROR) == B_ENTRY_CREATED) {
  	  	entry_ref ref;
  	  	ref.device = message->GetInt32("device", B_ERROR);
  	  	ref.directory = message->GetInt64("directory", B_ERROR);
  	  	BString name;
  	  	if (message->FindString("name", &name) == B_OK)
  	      ref.set_name(name.String());
  	    BEntry entry(&ref);
  	    if (entry.InitCheck() == B_OK && entry.Exists()) {
  	      BNode node(&ref);
  	      BString cypherkey;
  	      if (node.ReadAttrString("cypherkey", &cypherkey) != B_OK)
  	        return;
  	      for (auto item = this->wanted.begin(); item != this->wanted.end(); item++) {
  	      	if (std::get<0>(*item) == cypherkey) {
  	      	  for (auto &target : std::get<3>(*item))
  	      	    target.SendMessage(message);
  	      	  this->wanted.erase(item);
  	      	  return;
  	      	}
  	      }
  	    }
  	  }
  	} break;
  }
}

void Wanted::addWant(BString &cypherkey, int8 distance, BMessenger replyTo) {
  for (auto &item : this->wanted) {
    BString &existingKey = std::get<0>(item);
    if (existingKey == cypherkey) {
      std::get<1>(item) = std::min(std::get<1>(item), distance);
      std::vector<BMessenger> &replyTargets = std::get<3>(item);
      std::remove_if(
          std::get<3>(item).begin(), std::get<3>(item).end(),
          [](auto existingTarget) { return !existingTarget.IsValid(); });
      if (replyTo.IsValid())
        std::get<3>(item).push_back(replyTo);
      return;
    }
  }
  {
    BQuery query;
    query.SetVolume(&this->volume);
    query.PushAttr("HABITAT:cypherkey");
    query.PushString(cypherkey.String());
    query.PushOp(B_EQ);
    if (this->Looper())
      query.SetTarget(BMessenger(this));
    if (query.Fetch() == B_OK)
      this->wanted.push_back({cypherkey, distance, query, {replyTo}});
  }
}
} // namespace blob