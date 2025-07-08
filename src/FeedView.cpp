#include "FeedView.h"
#include "Logging.h"
#include "Plugin.h"
#include <ScrollView.h>
#include <functional>

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

const std::map<BString, std::function<BView *(BMessage *)>> &messageTypes() {
  static auto instance = initTypes();
  return instance;
}
}; // namespace

FeedView::FeedView(const BMessage &specifier)
    :
    BGroupView(B_VERTICAL) {
  this->setSpecifier(specifier);
}

FeedView::~FeedView() {
  if (this->doneMessenger.IsValid())
    this->doneMessenger.SendMessage('STOP');
}

void FeedView::AttachedToWindow() {
  BGroupView::AttachedToWindow();
  if (!messageTypes().empty()) {
    BMessage rq(B_GET_PROPERTY);
    rq.AddSpecifier(&this->specifier);
    rq.AddMessenger("target", BMessenger(this));
    rq.AddBool("includeKey", true);
    BMessenger("application/x-vnd.habitat").SendMessage(&rq, BMessenger(this));
  }
}

void FeedView::MessageReceived(BMessage *message) {
  if (BMessenger result; message->FindMessenger("result", &result) == B_OK) {
    this->doneMessenger = result;
  } else if (BMessage content;
             message->FindMessage("content", &content) == B_OK ||
             message->FindMessage("cleartext", &content) == B_OK) {
    if (BString msgType; content.FindString("type", &msgType) == B_OK) {
      if (auto vc = messageTypes().find(msgType); vc != messageTypes().end()) {
        if (auto v = vc->second(message); v != NULL) {
          // TODO: Sort the messages
          this->GroupLayout()->AddView(v, 1.0f);
          if (this->Parent())
            this->Parent()->InvalidateLayout();
          this->updateScroll();
        }
      }
    }
  } else {
    BGroupView::MessageReceived(message);
  }
}

status_t FeedView::setSpecifier(const BMessage &specifier) {
  this->specifier = specifier;
  if (BString dummy; this->specifier.what == 'CPLX' &&
      this->specifier.FindString("type", &dummy) == B_NAME_NOT_FOUND) {
    for (auto &[typeName, vc] : messageTypes())
      this->specifier.AddString("type", typeName);
  }
  this->specifier.SetString("property", "Post");
  // TODO: Return error if the specifier is not understood,
  return B_OK;
}

void FeedView::GetHeightForWidth(float width, float *min, float *max,
                                 float *preferred) {
  if (auto layout = static_cast<BTwoDimensionalLayout *>(this->GetLayout());
      layout) {
    layout->GetHeightForWidth(width, min, max, preferred);
  } else {
    BView::GetHeightForWidth(width, min, max, preferred);
  }
}

bool FeedView::HasHeightForWidth() {
  if (auto layout = static_cast<BTwoDimensionalLayout *>(this->GetLayout());
      layout) {
    return layout->HasHeightForWidth();
  } else {
    return BView::HasHeightForWidth();
  }
}

void FeedView::GetPreferredSize(float *width, float *height) {
  if (width)
    *width = B_SIZE_UNLIMITED;
  if (height)
    *height = B_SIZE_UNSET;
}

void FeedView::FrameResized(float newWidth, float newHeight) {
  BGroupView::FrameResized(newWidth, newHeight);
  this->updateScroll();
}

void FeedView::updateScroll() {
  auto sv = dynamic_cast<BScrollView *>(this->Parent());
  if (!sv)
    return;
  auto scrollBar = sv->ScrollBar(B_VERTICAL);
  if (!scrollBar)
    return;
  float viewHeight;
  BRect bounds = this->Bounds();
  this->GetHeightForWidth(bounds.Width(), NULL, NULL, &viewHeight);
  float windowHeight = bounds.Height();
  if (viewHeight > windowHeight) {
    scrollBar->SetRange(0, viewHeight - windowHeight);
    scrollBar->SetSteps(20, windowHeight * 0.8f);
    scrollBar->SetProportion(windowHeight / viewHeight);
  } else {
    scrollBar->SetRange(0, 0);
    scrollBar->SetProportion(1.0f);
  }
}
