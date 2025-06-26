#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "FeedView.h"
#include "Post.h"
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <StatusBar.h>
#include <Window.h>
#include <unicode/timezone.h>

class MainWindow : public BWindow {
public:
  MainWindow(SSBDatabase *db);
  void MessageReceived(BMessage *message) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  status_t GetSupportedSuites(BMessage *data) override;

private:
  std::unique_ptr<U_ICU_NAMESPACE::TimeZone> tz;
  BMenuBar *menuBar;
  BStatusBar *statusBar;
  BScrollView *contents;
  FeedView *feed;
};

#endif
