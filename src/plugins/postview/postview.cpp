#include "Markdown.h"
#include <DateTimeFormat.h>
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
  PostDisplay(const BString &body, const BString &author, const BString &key,
              int64 timestamp);
  ~PostDisplay();
  void GetHeightForWidth(float width, float *min, float *max,
                         float *preferred) override;
  bool HasHeightForWidth() override;

private:
  BStringView *authorLabel;
  BStringView *authorValue;
  BStringView *dateLabel;
  BStringView *dateValue;
  MarkdownView *body;
  int64 timestamp;
};

BString formatTimestamp(int64 timestamp) {
  BDateTimeFormat formatter;
  time_t seconds = timestamp / 1000;
  if (BString result; formatter.Format(result, seconds, B_LONG_DATE_FORMAT,
                                       B_MEDIUM_TIME_FORMAT) == B_OK) {
    return result;
  }
  return "Error";
}

PostDisplay::PostDisplay(const BString &body, const BString &author,
                         const BString &key, int64 timestamp)
    :
    BView("", B_SUPPORTS_LAYOUT | B_WILL_DRAW),
    // TODO: Localize labels
    authorLabel(new BStringView("authorLabel", "Author:")),
    authorValue(new BStringView("authorValue", author)),
    dateLabel(new BStringView("dateLabel", "Date:")),
    dateValue(new BStringView("dateValue", formatTimestamp(timestamp))),
    body(new MarkdownView(body)),
    timestamp(timestamp) {
  this->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
  this->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
  this->SetExplicitPreferredSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
  BFont labelFont;
  this->GetFont(&labelFont);
  labelFont.SetFace(B_BOLD_FACE);
  this->authorLabel->SetFont(&labelFont);
  this->dateLabel->SetFont(&labelFont);
  BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
      .SetInsets(15.0)
      .AddGrid(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
      .Add(this->authorLabel, 0, 0)
      .Add(this->authorValue, 1, 0)
      .Add(this->dateLabel, 0, 1)
      .Add(this->dateValue, 1, 1)
      .End()
      .AddGlue()
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
  BString author;
  message->FindString("author", &author);
  BString key;
  message->FindString("cypherkey", &key);
  BString text;
  content.FindString("text", &text);
  double timestamp;
  message->FindDouble("timestamp", &timestamp);
  auto result = new PostDisplay(text, author, key, (int64)timestamp);
  // TODO: root, branch, etc
  return result;
}
} // namespace

extern "C" status_t
registerViewer(std::map<BString, std::function<BView *(BMessage *)>> *reg) {
  reg->insert({BString("post"), std::function(mkDisplay)});
  return B_OK;
}
