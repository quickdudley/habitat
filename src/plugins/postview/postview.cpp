#include "Markdown.h"
#include <Layout.h>
#include <View.h>
#include <functional>
#include <map>
#include <utility>

extern "C" const char *pluginName() { return "post-view"; }

namespace {
class PostDisplay : public BView {
public:
  PostDisplay(std::vector<std::unique_ptr<markdown::BlockNode>> body,
              const BString &author, const BString &key, int64 timestamp);

private:
  std::vector<std::unique_ptr<markdown::BlockNode>> body;
  BString author;
  BString key;
  int64 timestamp;
};

PostDisplay::PostDisplay(std::vector<std::unique_ptr<markdown::BlockNode>> body,
                         const BString &author, const BString &key,
                         int64 timestamp)
    :
    BView("", B_SUPPORTS_LAYOUT | B_WILL_DRAW),
    body(std::move(body)),
    author(author),
    key(key),
    timestamp(timestamp) {}

BView *mkDisplay(BMessage *message) {
  BMessage content;
  if (message->FindMessage("content", &content) != B_OK &&
      message->FindMessage("cleartext", &content) != B_OK) {
    return NULL;
  }
  BString author;
  message->FindString("author", &author);
  BString key;
  message->FindString("cypherkey", &key);
  BString text;
  content.FindString("text", &key);
  double timestamp;
  message->FindDouble("timestamp", &timestamp);
  auto result =
      new PostDisplay(markdown::parse(text), author, key, (int64)timestamp);
  // TODO: root, branch, etc
  return result;
}
} // namespace

extern "C" status_t
registerViewer(std::map<BString, std::function<BView *(BMessage *)>> *reg) {
  reg->insert({BString("post"), std::function(mkDisplay)});
  return B_OK;
}
