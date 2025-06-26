#ifndef FEEDVIEW_H
#define FEEDVIEW_H

#include <View.h>

class FeedView : public BView {
public:
  FeedView(const char *name, uint32 flags, BLayout *layout = NULL);
};

#endif
