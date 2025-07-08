#ifndef FEEDVIEW_H
#define FEEDVIEW_H

#include <GroupView.h>
#include <Messenger.h>
#include <map>

class FeedView : public BGroupView {
public:
  FeedView(const BMessage &specifier);
  ~FeedView();
  void AttachedToWindow() override;
  void MessageReceived(BMessage *message) override;
  status_t setSpecifier(const BMessage &specifier);
  bool HasHeightForWidth() override;
  void GetHeightForWidth(float width, float *min, float *max,
                         float *preferred) override;

private:
  void setQuery();
  BMessage specifier;
  BMessenger doneMessenger;
};

#endif
