#include "Markdown.h"
#include "MessageHeader.h"
#include <GroupLayout.h>
#include <Layout.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <functional>
#include <map>
#include <utility>

extern "C" const char *pluginName() { return "post-view"; }

namespace {
class PostDisplay : public BView {
public:
  PostDisplay(const BString &body, const BMessage &message);
  ~PostDisplay();
  void GetHeightForWidth(float width, float *min, float *max,
                         float *preferred) override;
  bool HasHeightForWidth() override;

private:
  MessageHeader *header;
  MarkdownView *body;
  int64 timestamp;
};

PostDisplay::PostDisplay(const BString &body, const BMessage &message)
    :
    BView("", B_SUPPORTS_LAYOUT | B_WILL_DRAW),
    header(new MessageHeader(message)),
    body(new MarkdownView(body)) {
  this->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
  this->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
  this->SetExplicitPreferredSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
  BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
      .SetInsets(15.0)
      .Add(this->header)
      .Add(this->body)
      .End();
}

PostDisplay::~PostDisplay() {}

void PostDisplay::GetHeightForWidth(float width, float *min, float *max,
                                    float *preferred) {
  if (auto layout = static_cast<BTwoDimensionalLayout *>(this->GetLayout());
      layout) {
    layout->GetHeightForWidth(width, min, max, preferred);
  } else {
    BView::GetHeightForWidth(width, min, max, preferred);
  }
}

bool PostDisplay::HasHeightForWidth() {
  if (auto layout = static_cast<BTwoDimensionalLayout *>(this->GetLayout());
      layout) {
    return layout->HasHeightForWidth();
  } else {
    return BView::HasHeightForWidth();
  }
}

BView *mkDisplay(BMessage *message) {
  BMessage content;
  if (message->FindMessage("content", &content) != B_OK &&
      message->FindMessage("cleartext", &content) != B_OK) {
    return NULL;
  }
  BString text;
  content.FindString("text", &text);
  auto result = new PostDisplay(text, *message);
  // TODO: root, branch, etc
  return result;
}
} // namespace

extern "C" status_t
registerViewer(std::map<BString, std::function<BView *(BMessage *)>> *reg) {
  reg->insert({BString("post"), std::function(mkDisplay)});
  return B_OK;
}
