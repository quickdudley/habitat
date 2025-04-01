#ifndef MAIN_H
#define MAIN_H

#include "Blob.h"
#include "ContactStore.h"
#include "EBT.h"
#include "Lan.h"
#include "Listener.h"
#include "Post.h"
#include "Secret.h"
#include <Application.h>
#include <Directory.h>
#include <MenuBar.h>
#include <Path.h>
#include <StatusBar.h>
#include <Window.h>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <unicode/timezone.h>

class MainWindow : public BWindow {
public:
  MainWindow(SSBDatabase *db);
  void MessageReceived(BMessage *message) override;

private:
  BMenuBar *menuBar;
  BStatusBar *statusBar;
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
  virtual void pack(BMessage *record, bool includeStatus = true);
  virtual status_t update(const BMessage *record);
  virtual BString fullName();
  virtual void connect();

private:
  BString transport;
  BString hostname;
  BString cypherkey;
  bool connected = false;
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
  void acceptConnection(BDataIO *, std::function<void()> closeHook = NULL);
  void initiateConnection(BDataIO *, const BString &key,
                          std::function<void()> closeHook = NULL);

private:
  void loadSettings();
  void saveSettings();
  void checkServerStatus();
  static int initiateConnection(void *message);
  static int accept__(void *args);
  static int initiate__(void *args);
  int initiate__(std::unique_ptr<BDataIO> &, unsigned char *key,
                 std::function<void()> closeHook = NULL);
  MainWindow *mainWindow;
  SSBDatabase *databaseLooper;
  ContactStore *contactStore;
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
  std::default_random_engine rng;
};

extern Habitat *app;

#endif
