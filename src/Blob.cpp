#include "Blob.h"
#include <algorithm>

namespace blob {

void Wanted::MessageReceived(BMessage *message) {}

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