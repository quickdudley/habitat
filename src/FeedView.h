#ifndef FEEDVIEW_H
#define FEEDVIEW_H

#include <View.h>

class FeedView : public BView {
public:
  FeedView(const char *name, const BMessage &specifier, uint32 flags,
           BLayout *layout = NULL);

private:
  BMessage specifier;
};

#endif
