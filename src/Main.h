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
#include <MenuBar.h>
#include <Path.h>
#include <Window.h>
#include <memory>
#include <set>
#include <unicode/timezone.h>

class MainWindow : public BWindow {
public:
  MainWindow(void);
  void MessageReceived(BMessage *message) override;

private:
  BMenuBar *menuBar;
};

class Habitat;

class ServerRecord {
public:
  ServerRecord();
  ServerRecord(const BString &hostname, const BString &cypherkey);
  ServerRecord(const BString &transport, const BString &hostname,
               const BString &cypherkey);
  ServerRecord(BMessage *record);
  virtual ~ServerRecord() {}
  virtual bool isValid();
  virtual void pack(BMessage *record);
  virtual status_t update(const BMessage *record);
  virtual BString fullName();

private:
  BString transport;
  BString hostname;
  BString cypherkey;
  friend class Habitat;
};

class Habitat : public BApplication {
public:
  Habitat(void);
  status_t GetSupportedSuites(BMessage *data);
  void MessageReceived(BMessage *msg);
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property);
  void ReadyToRun();
  void Quit();
  BDirectory &settingsDir();

private:
  void loadSettings();
  void saveSettings();
  static int initiateConnection(void *message);
  MainWindow *mainWindow;
  SSBDatabase *databaseLooper;
  OwnFeed *ownFeed;
  ebt::Dispatcher *ebt;
  blob::Wanted *wantedBlobs;
  muxrpc::MethodSuite serverMethods;
  muxrpc::MethodSuite clientMethods;
  std::unique_ptr<BDirectory> settings;
  std::unique_ptr<U_ICU_NAMESPACE::TimeZone> tz;
  std::unique_ptr<SSBListener> ipListener;
  std::unique_ptr<BHandler> lanBroadcaster;
  std::shared_ptr<Ed25519Secret> myId;
  std::vector<ServerRecord> servers;
  std::set<void *> cloggedChannels;
};

extern Habitat *app;

#endif
