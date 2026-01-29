#ifndef MESSAGE_HEADER_H
#define MESSAGE_HEADER_H

#include <Bitmap.h>
#include <StringView.h>
#include <memory>

class UserPicture : public BView {
public:
  UserPicture(std::shared_ptr<BBitmap> source);
  void Draw(BRect updateRect) override;
  void setSource(std::shared_ptr<BBitmap> source);

private:
  std::shared_ptr<BBitmap> source;
};

class MessageHeader : public BView {
public:
  MessageHeader(const BMessage &message);
  ~MessageHeader();
  void AttachedToWindow() override;
  void MessageReceived(BMessage *message) override;

private:
  UserPicture *userPicture;
  BStringView *authorValue;
  BStringView *dateValue;
};

#endif // MESSAGE_HEADER_H
