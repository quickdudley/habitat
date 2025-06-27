#include <Layout.h>
#include <View.h>
#include <functional>
#include <map>

extern "C" const char *pluginName() { return "post-view"; }

namespace {

BView *mkDisplay(BMessage *message) { return NULL; }
} // namespace

extern "C" status_t
registerViewer(std::map<BString, std::function<BView *(BMessage *)>> *reg) {
  reg->insert({BString("post"), std::function(mkDisplay)});
  return B_OK;
}
