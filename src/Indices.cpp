#include "Indices.h"
#include <SupportDefs.h>
#include <TypeConstants.h>
#include <cstring>
#include <errno.h>
#include <fs_index.h>
#include <fs_info.h>
#include <memory>

struct RequiredIndex {
  const char *name;
  type_code type;
};

// TODO: Remove most of these
static RequiredIndex requiredIndices[] = {
    {"HABITAT:cypherkey", B_STRING_TYPE}, {"HABITAT:sequence", B_INT64_TYPE},
    {"HABITAT:author", B_STRING_TYPE},    {"HABITAT:timestamp", B_INT64_TYPE},
    {"HABITAT:type", B_STRING_TYPE},      {"HABITAT:context", B_STRING_TYPE}};
namespace {
struct IndexDirDeleter {
  void operator()(DIR *p) { fs_close_index_dir(p); }
};
} // namespace

#define HABITAT_INDEX_COUNT (sizeof(requiredIndices) / sizeof(RequiredIndex))

void ensureIndices(const char *path) {
  bool exists[HABITAT_INDEX_COUNT] = {false};
  dev_t device = dev_for_path(path);
  std::unique_ptr<DIR, IndexDirDeleter> indices =
      std::unique_ptr<DIR, IndexDirDeleter>(fs_open_index_dir(device));
  if (indices == NULL)
    throw UNINDEXABLE_VOLUME;
  while (true) {
    errno = 0;
    dirent *index = fs_read_index_dir(indices.get());
    if (index == NULL) {
      if (errno != B_ENTRY_NOT_FOUND && errno != B_OK) {
        status_t z = errno;
        errno = 0;
        throw z;
      }
      break;
    }
    index_info info;
    if (fs_stat_index(device, index->d_name, &info) != B_OK)
      throw errno;
    for (int i = 0; i < HABITAT_INDEX_COUNT; i++)
      if (strcmp(requiredIndices[i].name, index->d_name) == 0)
        exists[i] = true;
  }
  for (int i = 0; i < sizeof(requiredIndices) / sizeof(RequiredIndex); i++) {
    if (!exists[i]) {
      fs_create_index(device, requiredIndices[i].name, requiredIndices[i].type,
                      0);
    }
  }
}
