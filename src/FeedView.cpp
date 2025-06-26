#include "FeedView.h"
#include "Plugin.h"
#include <functional>
#include <map>

namespace {

std::map<BString, std::function<BView *(BMessage *)>> initTypes() {
  typedef status_t (*pview_t)(
      std::map<BString, std::function<BView *(BMessage *)>> *);
  std::map<BString, std::function<BView *(BMessage *)>> result;
  for (auto &[pname, sym] : habitat_plugins.lookup("registerViewer")) {
    auto f = (pview_t)sym;
    f(&result);
  }
  return result;
}
std::map<BString, std::function<BView *(BMessage *)>> messageTypes =
    initTypes();
}; // namespace

FeedView::FeedView(const char *name, const BMessage &specifier, uint32 flags,
                   BLayout *layout)
    :
    BView(name, flags, layout),
    specifier(specifier) {
  if (BString dummy; this->specifier.what == 'CPLX' &&
      this->specifier.FindString("type", &dummy) == B_NAME_NOT_FOUND) {
    for (auto &[typeName, vc] : messageTypes)
      this->specifier.AddString("type", typeName);
  }
}
