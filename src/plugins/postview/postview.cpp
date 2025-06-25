#include <Layout.h>
#include <View.h>
#include <functional>
#include <map>

extern "C" const char *pluginName() { return "post-view"; }

extern "C" status_t
registerViewer(std::map<BString, std::function<BView *(BMessage *)>> *reg) {
  return B_OK;
}
