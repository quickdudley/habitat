#ifndef MAIN_H
#define MAIN_H

#include <Application.h>
#include <Directory.h>
#include <Path.h>
#include <Window.h>
#include <memory>
#include <unicode/timezone.h>

class MainWindow : public BWindow {
public:
  MainWindow(void);
};

class Habitat : public BApplication {
public:
  Habitat(void);
  status_t GetSupportedSuites(BMessage *data);
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property);
  void MessageReceived(BMessage *msg);

private:
  MainWindow *mainWindow;
  std::unique_ptr<BDirectory> settings;
  std::unique_ptr<U_ICU_NAMESPACE::TimeZone> tz;
};

extern Habitat *app;

#endif
