#ifndef MAIN_H
#define MAIN_H

#include "ContactStore.h"
#include "EBT.h"
#include "Lan.h"
#include "Listener.h"
#include "MainWindow.h"
#include "Post.h"
#include "ProfileStore.h"
#include "Secret.h"
#include <Application.h>
#include <Directory.h>
#include <Path.h>
#include <functional>
#include <memory>
#include <random>
#include <set>

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
  status_t GetSupportedSuites(BMessage *data) override;
  void MessageReceived(BMessage *msg) override;
  BHandler *ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
                             int32 what, const char *property) override;
  void ReadyToRun() override;
  void Quit() override;
  BDirectory &settingsDir();
  void acceptConnection(BDataIO *, std::function<void()> closeHook = NULL);
  void initiateConnection(BDataIO *, const BString &key,
                          std::function<void()> closeHook = NULL);
  BMessenger addWorker(BHandler *w);

private:
  void loadSettings();
  void saveSettings();
  void checkServerStatus();
  static int initiateConnection(void *message);
  static int accept__(void *args);
  static int initiate__(void *args);
  int initiate__(std::unique_ptr<BDataIO>, unsigned char *key,
                 std::function<void()> closeHook = NULL);
  MainWindow *mainWindow;
  BLooper *worker;
  SSBDatabase *databaseLooper;
  ContactStore *contactStore;
  ProfileStore *profileStore;
  OwnFeed *ownFeed;
  ebt::Dispatcher *ebt;
  blob::Wanted *wantedBlobs;
  muxrpc::MethodSuite serverMethods;
  muxrpc::MethodSuite clientMethods;
  std::unique_ptr<BDirectory> settings;
  std::unique_ptr<SSBListener> ipListener;
  std::unique_ptr<BHandler> lanBroadcaster;
  std::shared_ptr<Ed25519Secret> myId;
  std::vector<ServerRecord> servers;
  std::set<void *> cloggedChannels;
  std::default_random_engine rng;
};

extern Habitat *app;

#endif
