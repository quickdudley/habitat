#include "MessageHeader.h"
#include <DateTimeFormat.h>
#include <LayoutBuilder.h>
#include <MimeType.h>
#include <cmath>
#include <translation/TranslationUtils.h>

namespace {
// TODO: Make sure this handles daylight savings time.
BString formatTimestamp(int64 timestamp) {
  BDateTimeFormat formatter;
  time_t seconds = timestamp / 1000;
  if (BString result; formatter.Format(result, seconds, B_LONG_DATE_FORMAT,
                                       B_MEDIUM_TIME_FORMAT) == B_OK) {
    return result;
  }
  return "Error";
}

std::shared_ptr<BBitmap> makePersonIcon() {
  std::shared_ptr<BBitmap> result =
      std::make_shared<BBitmap>(BRect(0, 0, 31, 31), B_RGBA32);
  BMimeType mimeType("application/x-person");
  if (mimeType.GetIcon(result.get(), B_LARGE_ICON) != B_OK)
    result = NULL;
  return result;
}
} // namespace

MessageHeader::MessageHeader(const BMessage &message)
    : BView("", B_SUPPORTS_LAYOUT | B_WILL_DRAW) {
  BString author;
  if (message.FindString("author", &author) != B_OK)
    author = "Error";

  BString datetime;
  {
    if (double timestamp; message.FindDouble("timestamp", &timestamp) == B_OK)
      datetime = formatTimestamp(timestamp);
    else
      datetime = "Error";
  }
  this->authorValue = new BStringView("authorValue", author);
  this->dateValue = new BStringView("dateValue", datetime);
  static std::shared_ptr<BBitmap> personIcon = makePersonIcon();
  this->userPicture = new UserPicture(personIcon);
  this->userPicture->SetToolTip(author);
  BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
      .Add(this->userPicture, 0, 0, 1, 2)
      .Add(this->authorValue, 1, 0)
      .Add(this->dateValue, 1, 1)
      .AddGlue(2, 0)
      .End();
}

MessageHeader::~MessageHeader() {}

void MessageHeader::AttachedToWindow() {
  BMessage rq(B_GET_PROPERTY);
  rq.AddSpecifier("Profile", this->authorValue->Text());
  BMessenger("application/x-vnd.habitat").SendMessage(&rq, this);
  BView::AttachedToWindow();
}

void MessageHeader::MessageReceived(BMessage *message) {
  if (!message->IsReply())
    return BView::MessageReceived(message);
  if (BMessage result; message->FindMessage("result", &result) == B_OK) {
    BString author;
    if (result.FindString("about", &author) != B_OK ||
        author != this->authorValue->Text()) {
      return BView::MessageReceived(message);
    }
    BString name;
    if (result.FindString("name", &name) == B_OK && name.Length() != 0) {
      this->authorValue->SetText(name);
      this->authorValue->SetToolTip(author);
    }
    BString imageHash;
    if (BMessage image; result.FindMessage("image", &image) == B_OK)
      image.FindString("link", &imageHash);
    if (imageHash.Length() != 0 ||
        result.FindString("image", &imageHash) == B_OK) {
      BMessage rq(B_CREATE_PROPERTY);
      rq.AddSpecifier("Blob");
      rq.AddString("cypherkey", imageHash);
      BMessenger("application/x-vnd.habitat").SendMessage(&rq, this);
    }
  } else if (entry_ref result; message->FindRef("result", &result) == B_OK) {
    // TODO: Factor out this logic into something reusable.
    BBitmap *unscaled = BTranslationUtils::GetBitmap(&result, NULL);
    float scale = 1.0;
    BRect bounds(unscaled->Bounds());
    {
      float s = 32.0 / bounds.Width();
      if (scale > s)
        scale = s;
      s = 32.0 / bounds.Height();
      if (scale > s)
        scale = s;
    }
    auto scaled = std::make_shared<BBitmap>(
        BRect(0, 0, std::floor(bounds.Width() * scale - 1),
              std::floor(bounds.Height() * scale - 1)),
        B_RGBA32, true);
    auto view =
        new BView(scaled->Bounds(), "scale", B_FOLLOW_NONE, B_WILL_DRAW);
    scaled->AddChild(view);
    scaled->Lock();
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
    view->DrawBitmap(unscaled, unscaled->Bounds(), scaled->Bounds());
    view->Sync();
    scaled->Unlock();
    scaled->RemoveChild(view);
    delete view;
    this->userPicture->setSource(scaled);
  } else {
    return BView::MessageReceived(message);
  }
}

UserPicture::UserPicture(std::shared_ptr<BBitmap> source)
    : BView("User picture",
            B_FULL_UPDATE_ON_RESIZE | B_WILL_DRAW | B_SUPPORTS_LAYOUT) {
  this->setSource(source);
}

void UserPicture::setSource(std::shared_ptr<BBitmap> source) {
  this->source = source;
  auto bounds = source->Bounds();
  this->SetExplicitSize(bounds.Size());
  this->Invalidate();
}

// TODO: Handle animated gifs.
void UserPicture::Draw(BRect updateRect) {
  if (this->source != NULL) {
    this->SetDrawingMode(B_OP_ALPHA);
    this->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
    this->DrawBitmap(this->source.get(), BPoint(0, 0));
  }
}
