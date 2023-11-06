#ifndef MAIN_H
#define MAIN_H

#include "Blob.h"
#include "EBT.h"
#include "Lan.h"
#include "Listener.h"
#include "Post.h"
#include "Secret.h"
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
  void MessageReceived(BMessage *msg);
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property);
  thread_id Run();
  void ReadyToRun();
  void Quit();

private:
  MainWindow *mainWindow;
  SSBDatabase *databaseLooper;
  OwnFeed *ownFeed;
  ebt::Dispatcher *ebt;
  blob::Wanted *wantedBlobs;
  muxrpc::MethodSuite serverMethods;
  std::unique_ptr<BDirectory> settings;
  std::unique_ptr<BDirectory> postDir;
  std::unique_ptr<U_ICU_NAMESPACE::TimeZone> tz;
  std::unique_ptr<SSBListener> ipListener;
  std::unique_ptr<BHandler> lanBroadcaster;
  std::shared_ptr<Ed25519Secret> myId;
};

extern Habitat *app;

#endif
