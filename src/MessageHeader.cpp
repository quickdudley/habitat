#include "MessageHeader.h"
#include <Catalog.h>
#include <DateTimeFormat.h>
#include <LayoutBuilder.h>

// TODO: Add translation data to libhabitat so this will work
#define B_TRANSLATION_CONTEXT "MessageHeader"

namespace {
BString formatTimestamp(int64 timestamp) {
  BDateTimeFormat formatter;
  time_t seconds = timestamp / 1000;
  if (BString result; formatter.Format(result, seconds, B_LONG_DATE_FORMAT,
                                       B_MEDIUM_TIME_FORMAT) == B_OK) {
    return result;
  }
  return "Error";
}
}

MessageHeader::MessageHeader(const BMessage &message) : BView("", B_SUPPORTS_LAYOUT | B_WILL_DRAW) {
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
  // TODO: Translation macros
  // TODO: Profile lookup
  this->authorLabel = new BStringView("authorLabel", "Author:");
  this->authorValue = new BStringView("authorValue", author);
  this->authorName = new BStringView("authorName", "…");
  this->dateLabel = new BStringView("dateLabel", "Date:");
  this->dateValue = new BStringView("dateValue", datetime);
  BFont labelFont;
  this->GetFont(&labelFont);
  labelFont.SetFace(B_BOLD_FACE);
  this->authorLabel->SetFont(&labelFont);
  this->dateLabel->SetFont(&labelFont);
  BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
      .Add(this->authorLabel, 0, 0, 1, 2)
      .Add(this->authorValue, 1, 0)
      .Add(this->authorName, 1, 1)
      .Add(this->dateLabel, 0, 2)
      .Add(this->dateValue, 1, 2)
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
  BMessage result;
  if (message->FindMessage("result", &result) != B_OK)
    return BView::MessageReceived(message);
  BString author;
  if (result.FindString("about", &author) != B_OK || author != this->authorValue->Text())
    return BView::MessageReceived(message);
  BString name;
  if (result.FindString("name", &name) == B_OK)
    this->authorName->SetText(name);
  else
    this->authorName->SetText("‽");
}

#undef B_TRANSLATION_CONTEXT
