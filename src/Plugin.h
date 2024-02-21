#ifndef PLUGIN_H
#define PLUGIN_H

#include <String.h>
#include <utility>
#include <vector>

class Plugins {
public:
  Plugins();
  std::vector<std::pair<const BString &, void *>> lookup(const char *symbol);

private:
  std::vector<std::pair<BString, void *>> plugins;
};

extern Plugins habitat_plugins;

#endif // PLUGIN_H
