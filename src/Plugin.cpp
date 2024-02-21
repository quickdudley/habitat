#include "Plugin.h"
#include <Directory.h>
#include <Entry.h>
extern "C" {
#include <dlfcn.h>
}
#include <cstdlib>
#include <set>

Plugins habitat_plugins;

Plugins::Plugins() {
  BString libpath(std::getenv("LIBRARY_PATH"));
  std::set<BString> names;
  int32 pathOffset = 0;
  while (pathOffset < libpath.Length()) {
    int32 colon = libpath.FindFirst(':', pathOffset);
    int32 segmentEnd = colon == B_ERROR ? libpath.Length() : colon;
    BString segment;
    libpath.CopyInto(segment, pathOffset, segmentEnd - pathOffset);
    pathOffset = segmentEnd + 1;
    segment.Append("/habitat_plugins");
    BDirectory scandir;
    if (scandir.SetTo(segment.String()) != B_OK)
      continue;
    BEntry entry;
    while (scandir.GetNextEntry(&entry) == B_OK) {
      BString filename;
      {
        char buffer[B_FILE_NAME_LENGTH];
        if (entry.GetName(buffer) != B_OK)
          continue;
        filename.SetTo(buffer);
      }
      if (!filename.EndsWith(".so"))
        continue;
      filename.Prepend("/");
      filename.Prepend(segment);
      void *handle = dlopen(filename.String(), RTLD_LAZY | RTLD_LOCAL);
      if (!handle)
        continue;
      typedef const char *(*nameCall_t)();
      nameCall_t nameCall = (nameCall_t)dlsym(handle, "pluginName");
      const char *pluginName = NULL;
      if (nameCall == NULL || (pluginName = nameCall()) == NULL ||
          !names.insert(pluginName).second) {
        dlclose(handle);
        continue;
      }
      this->plugins.push_back({pluginName, handle});
    }
  }
}

std::vector<std::pair<const BString &, void *>>
Plugins::lookup(const char *symbol) {
  std::vector<std::pair<const BString &, void *>> result;
  for (auto &plugin : this->plugins) {
    void *pointer = dlsym(plugin.second, symbol);
    if (pointer != NULL)
      result.push_back({plugin.first, pointer});
  }
  return result;
}
