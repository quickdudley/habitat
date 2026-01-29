#ifndef MESSAGE_HEADER_H
#define MESSAGE_HEADER_H

#include <StringView.h>

class MessageHeader : public BView {
public:
  MessageHeader(const BMessage &message);
  ~MessageHeader();
  void AttachedToWindow() override;
  void MessageReceived(BMessage *message) override;

private:
  BStringView *authorLabel;
  BStringView *authorValue;
  BStringView *authorName;
  BStringView *dateLabel;
  BStringView *dateValue;
};

#endif // MESSAGE_HEADER_H
