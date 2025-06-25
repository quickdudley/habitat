#include "Main.h"
#include "Base64.h"
#include "Connection.h"
#include "ContactGraph.h"
#include "Indices.h"
#include "Logging.h"
#include "MigrateDB.h"
#include "Room.h"
#include "SelectContacts.h"
#include "SettingsWindow.h"
#include <ByteOrder.h>
#include <Catalog.h>
#include <File.h>
#include <FindDirectory.h>
#include <LocaleRoster.h>
#include <MenuItem.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <TimeZone.h>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <sodium.h>
#include <string>

#define HABITAT_AUTO_CONNECTIONS 10
#define B_TRANSLATION_CONTEXT "MainWindow"

namespace {
class TidyLooper : public BLooper {
public:
  TidyLooper(const char *name);
  void Quit() override;
};
} // namespace

Habitat *app;

int main(int argc, const char **args) {
  signal(SIGPIPE, SIG_IGN);
  int exit_status = 0;
  if (sodium_init() == -1) {
    std::cerr << B_TRANSLATE("Failed to initialize libsodium") << std::endl;
    return -1;
  }
  try {
    app = new Habitat();
    app->Run();
  } catch (...) {
    exit_status = -1;
  }
  return exit_status;
}

enum {
  kTimeZone,
  kCypherkey,
  kCreateBlob,
  kCreatePost,
  kLogCategory,
  kServer,
  kConnection
};

static property_info habitatProperties[] = {
    {"Timezone",
     {B_GET_PROPERTY, B_SET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "The time zone used for date and time operations",
     kTimeZone,
     {B_STRING_TYPE}},
    {"Cypherkey",
     {B_GET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "The SSB identifier for this copy of Habitat",
     kCypherkey,
     {B_STRING_TYPE}},
    {"Post",
     {B_CREATE_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "Create a post on our own feed",
     kCreatePost,
     {}},
    {"Blob",
     {B_CREATE_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "Create a blob or register the fact we want one",
     kCreateBlob,
     {}},
    {"LogCategory",
     {B_CREATE_PROPERTY, B_DELETE_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, B_NAME_SPECIFIER, 0},
     "An enabled category of log entries",
     kLogCategory,
     {}},
    {"Server",
     {B_CREATE_PROPERTY, B_GET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "A pub server or room server",
     kServer,
     {}},
    {"Server",
     {B_DELETE_PROPERTY, B_GET_PROPERTY, B_SET_PROPERTY, 0},
     {B_NAME_SPECIFIER, 0},
     "A pub server or room server",
     kServer,
     {}},
    {"Connection",
     {B_CREATE_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "A one-time connection to another peer",
     kConnection,
     {}},
    {0}};

// TODO: Move most of this into ReadyToRun
Habitat::Habitat(void)
    :
    BApplication("application/x-vnd.habitat") {
  {
    std::random_device hwrng;
    this->rng.seed(hwrng());
  }
  this->AddHandler(new Logger());
  // Set timezone
  {
    BTimeZone defaultTimeZone;
    BLocaleRoster::Default()->GetDefaultTimeZone(&defaultTimeZone);
    this->tz = std::unique_ptr<U_ICU_NAMESPACE::TimeZone>(
        U_ICU_NAMESPACE::TimeZone::createTimeZone(
            defaultTimeZone.ID().String()));
  }
  BDirectory contactsDir;
  sqlite3 *database;
  {
    // Create settings directory
    BPath settings_path;
    find_directory(B_USER_SETTINGS_DIRECTORY, &settings_path, true);
    BDirectory settings_parent = BDirectory(settings_path.Path());
    BDirectory settings;
    status_t status = settings_parent.CreateDirectory("Habitat", &settings);
    if (status == B_FILE_EXISTS) {
      BEntry entry;
      status = settings_parent.FindEntry("Habitat", &entry, true);
      if (status != B_OK)
        throw status;
      this->settings = std::make_unique<BDirectory>(&entry);
    } else if (status == B_OK) {
      this->settings = std::make_unique<BDirectory>(settings);
    } else {
      throw status;
    }
    // Create indices
    ensureIndices(settings_path.Path());
    // Load secret if it exists
    this->myId = std::make_shared<Ed25519Secret>();
    BEntry secret;
    status = this->settings->FindEntry("secret", &secret, true);
    if (status == B_OK) {
      BPath path;
      secret.GetPath(&path);
      BFile secretFile(&secret, B_READ_ONLY);
      JSON::parse(std::make_unique<JSON::SecretNode>(this->myId.get()),
                  &secretFile);
    } else if (B_ENTRY_NOT_FOUND) {
      // Generate new secret
      BFile secretFile;
      status = this->settings->CreateFile("secret", &secretFile, true);
      if (status != B_OK)
        throw status;
      this->myId->generate();
      BString secretJson;
      JSON::RootSink sink(std::make_unique<JSON::SerializerStart>(&secretJson));
      this->myId->write(&sink);
      secretFile.WriteExactly(secretJson.String(), secretJson.Length(), NULL);
      secretFile.Sync();
    } else {
      throw status;
    }
  }
  // Create main feed looper
  {
    auto &settings = *this->settings;
    this->databaseLooper =
        new SSBDatabase([settings]() { return migrateToSqlite(settings); });
  }
  this->ownFeed = new OwnFeed(this->myId.get());
  this->databaseLooper->AddHandler(this->ownFeed);
  this->ownFeed->load();
  this->databaseLooper->loadFeeds();
  this->RegisterLooper(this->databaseLooper);
  this->contactStore = new ContactStore(this->databaseLooper->database);
  this->databaseLooper->AddHandler(this->contactStore);
  // Open main window
  this->mainWindow = new MainWindow(this->databaseLooper);
  this->mainWindow->Show();
}

status_t Habitat::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat");
  {
    BPropertyInfo propertyInfo(habitatProperties);
    data->AddFlat("messages", &propertyInfo);
  }
  {
    BPropertyInfo propertyInfo(databaseProperties);
    data->AddFlat("messages", &propertyInfo);
  }
  return BApplication::GetSupportedSuites(data);
}

BHandler *Habitat::ResolveSpecifier(BMessage *msg, int32 index,
                                    BMessage *specifier, int32 what,
                                    const char *property) {
  BPropertyInfo databaseInfo(databaseProperties);
  BPropertyInfo propertyInfo(habitatProperties);
  uint32 match;
  if (propertyInfo.FindMatch(msg, index, specifier, what, property, &match) >=
      0) {
    if (match == kCreatePost) {
      BMessenger(this->ownFeed).SendMessage(msg);
      return NULL;
    } else {
      return this;
    }
  }
  if (databaseInfo.FindMatch(msg, index, specifier, what, property, &match) >=
      0) {
    BMessenger(this->databaseLooper).SendMessage(msg);
    return NULL;
  }
  return BApplication::ResolveSpecifier(msg, index, specifier, what, property);
}

void Habitat::MessageReceived(BMessage *msg) {
  bool detached = false;
  if (!msg->HasSpecifiers()) {
    if (msg->what == 'LOG_') {
      for (int32 i = be_app->CountHandlers() - 1; i >= 0; i--) {
        if (Logger *logger = dynamic_cast<Logger *>(this->HandlerAt(i));
            logger != NULL) {
          BMessenger(logger).SendMessage(msg);
        }
      }
      return;
    } else if (msg->what == 'CLOG' && !msg->IsSourceRemote()) {
      bool currentlyClogged = !this->cloggedChannels.empty();
      void *channel;
      if (msg->FindPointer("channel", &channel) != B_OK)
        return;
      bool clogged;
      if (msg->FindBool("clogged", &clogged) != B_OK)
        return;
      if (clogged)
        this->cloggedChannels.insert(channel);
      else
        this->cloggedChannels.erase(channel);
      if (currentlyClogged == cloggedChannels.empty()) {
        BMessage forward('CLOG');
        forward.AddBool("clogged", !currentlyClogged);
        BMessenger(this->ebt).SendMessage(&forward);
      }
    } else {
      return BApplication::MessageReceived(msg);
    }
  }
  BMessage reply(B_REPLY);
  status_t error = B_ERROR;
  int32 index;
  BMessage specifier;
  int32 what;
  const char *property;
  uint32 match;
  if (msg->GetCurrentSpecifier(&index, &specifier, &what, &property) < 0)
    return BApplication::MessageReceived(msg);
  BPropertyInfo propertyInfo(habitatProperties);
  propertyInfo.FindMatch(msg, index, &specifier, what, property, &match);
  switch (match) {
  case kTimeZone:
    if (msg->what == B_SET_PROPERTY) {
      BString tz;
      if (msg->FindString("data", &tz) == B_OK) {
        U_ICU_NAMESPACE::TimeZone *utz =
            U_ICU_NAMESPACE::TimeZone::createTimeZone(
                U_ICU_NAMESPACE::UnicodeString::fromUTF8(tz.String()));
        if (utz) {
          this->tz = std::unique_ptr<U_ICU_NAMESPACE::TimeZone>(utz);
          error = B_OK;
        }
      }
    } else if (msg->what == B_GET_PROPERTY) {
      U_ICU_NAMESPACE::UnicodeString tzid;
      this->tz->getID(tzid);
      std::string tzidb;
      tzid.toUTF8String(tzidb);
      reply.AddString("result", tzidb.c_str());
      error = B_OK;
    }
    break;
  case kCypherkey: // Cypherkey
    reply.AddString("result", this->myId->getCypherkey());
    error = B_OK;
    break;
  case kCreateBlob:
    if (entry_ref ref; msg->FindRef("file", &ref) == B_OK) {
      // TODO: Include the cypherkey in the response
      error = this->wantedBlobs->hashFile(&ref);
      break;
    } else if (BString cypherkey;
               msg->FindString("cypherkey", &cypherkey) == B_OK) {
      auto h = new blob::LocalHandler(this->DetachCurrentMessage());
      this->AddHandler(h);
      this->wantedBlobs->addWant(cypherkey, 1, BMessenger(h));
      return;
    }
    error = B_OK;
    break;
  case kLogCategory: {
    int32 category;
    if (BString cascii; msg->FindString("category", &cascii) == B_OK ||
        specifier.FindString("name", &cascii) == B_OK) {
      if (cascii.Length() != 4) {
        error = B_BAD_VALUE;
        break;
      }
      category = *((int32 *)cascii.String());
      category = B_BENDIAN_TO_HOST_INT32(category);
    } else if (msg->FindInt32("category", &category) != B_OK) {
      error = B_BAD_VALUE;
      break;
    }
    for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
      if (Logger *logger = dynamic_cast<Logger *>(this->HandlerAt(i));
          logger != NULL) {
        if (msg->what == B_CREATE_PROPERTY) {
          logger->enableCategory(category);
          error = B_OK;
        } else if (msg->what == B_DELETE_PROPERTY) {
          logger->disableCategory(category);
          error = B_OK;
        }
      }
    }
  } break;
  case kServer: {
    switch (msg->what) {
    case B_CREATE_PROPERTY: {
      // TODO: Figure out whether to avoid subclassing or use alternate
      // approach to construction.
      this->servers.push_back(msg);
      if (!this->servers.back().isValid())
        this->servers.pop_back();
      else
        error = B_OK;
    } break;
    case B_DELETE_PROPERTY: {
      error = B_NAME_NOT_FOUND;
      for (auto i = this->servers.begin(); i != this->servers.end(); i++) {
        if (i->fullName() == specifier.GetString("name")) {
          error = B_OK;
          this->servers.erase(i);
          break;
        }
      }
    } break;
    case B_GET_PROPERTY: {
      error = B_NAME_NOT_FOUND;
      for (auto &server : this->servers) {
        if (what == B_DIRECT_SPECIFIER ||
            server.fullName() == specifier.GetString("name")) {
          BMessage result;
          server.pack(&result);
          reply.AddMessage("result", &result);
          error = B_OK;
          if (what == B_NAME_SPECIFIER)
            break;
        }
      }
    } break;
    case B_SET_PROPERTY: {
      error = B_NAME_NOT_FOUND;
      BMessage data;
      BString name = specifier.GetString("name");
      if (msg->FindMessage("data", &data) != B_OK) {
        error = B_BAD_VALUE;
        break;
      }
      for (auto &server : this->servers) {
        if (server.fullName() == name) {
          error = server.update(&data);
          break;
        }
      }
    }; break;
    }
    this->checkServerStatus();
  } break;
  case kConnection: {
    thread_id t = spawn_thread(Habitat::initiateConnection, "New connection", 0,
                               this->DetachCurrentMessage());
    detached = true;
    if (t < B_OK) {
      error = t;
      break;
    }
    if ((error = resume_thread(t)) < B_OK)
      break;
    return;
  }
  default:
    return BApplication::MessageReceived(msg);
  }
  reply.AddInt32("error", error);
  if (error != B_OK)
    reply.AddString("message", strerror(error));
  msg->SendReply(&reply);
  if (detached)
    delete msg;
}

void Habitat::checkServerStatus() {
  int alreadyConnected = 0;
  std::vector<ServerRecord *> candidates;
  for (auto &server : this->servers) {
    if (server.connected) {
      if (++alreadyConnected >= HABITAT_AUTO_CONNECTIONS)
        return;
    } else if (server.transport == "net") {
      candidates.push_back(&server);
    }
  }
  while (alreadyConnected++ < HABITAT_AUTO_CONNECTIONS && !candidates.empty()) {
    std::uniform_int_distribution<> distrib(0, candidates.size() - 1);
    size_t index = distrib(this->rng);
    candidates[index]->connect();
    candidates.erase(candidates.begin() + index);
  }
}

int Habitat::initiateConnection(void *message) {
  status_t error;
  BMessage *msg = (BMessage *)message;
  BMessage reply(B_REPLY);
  {
    BString host;
    if ((error = msg->FindString("host", &host)) != B_OK)
      goto sendReply;
    unsigned short port;
    if ((error = msg->FindUInt16("port", &port)) != B_OK &&
        (error = msg->FindInt16("port", (int16 *)&port)) != B_OK) {
      goto sendReply;
    }
    BString key;
    if ((error = msg->FindString("key", &key)) != B_OK)
      goto sendReply;
    auto rawKey = base64::decode(key);
    if (rawKey.size() != crypto_sign_PUBLICKEYBYTES) {
      error = B_BAD_VALUE;
      goto sendReply;
    }
    try {
      auto sock = std::make_unique<BSocket>(BNetworkAddress(host, port));
      sock->SetTimeout(15000000);
      BString name;
      static_cast<Habitat *>(be_app)->initiate__(
          std::move(sock), rawKey.data(),
          msg->FindString("name", &name) == B_OK
              ? std::function<void()>([name]() {
                  BMessage data;
                  data.AddBool("connected", false);
                  BMessage update(B_SET_PROPERTY);
                  update.AddMessage("data", &data);
                  update.AddSpecifier("Server", name);
                  BMessageRunner::StartSending(BMessenger(be_app), (&update),
                                               1000000, 1);
                })
              : NULL);
    } catch (...) {
      error = B_IO_ERROR;
      goto sendReply;
    }
    error = B_OK;
  }
sendReply:
  reply.AddInt32("error", error);
  if (error != B_OK)
    reply.AddString("message", strerror(error));
  msg->SendReply(&reply);
  if (BString name; error != B_OK && msg->FindString("name", &name) == B_OK) {
    BMessage data;
    data.AddBool("connected", false);
    BMessage update(B_SET_PROPERTY);
    update.AddMessage("data", &data);
    update.AddSpecifier("Server", name);
    BMessageRunner::StartSending(BMessenger(be_app), (&update), 15000000, 1);
  }
  delete msg;
  // TODO: Reap thread ID
  return 0;
}

namespace {
struct AcceptArgs {
  BDataIO *link;
  std::function<void()> closeHook;
};

struct InitiateArgs {
  BDataIO *link;
  unsigned char key[crypto_sign_PUBLICKEYBYTES];
  std::function<void()> closeHook;
};
} // namespace

void Habitat::acceptConnection(BDataIO *link, std::function<void()> closeHook) {
  auto args = new AcceptArgs{link, closeHook};
  thread_id t = spawn_thread(Habitat::accept__, "New Connection", 0, args);
  resume_thread(t);
}

void Habitat::initiateConnection(BDataIO *link, const BString &key,
                                 std::function<void()> closeHook) {
  auto args = new InitiateArgs{link, {0}, closeHook};
  auto rawKey = base64::decode(key);
  if (rawKey.size() != crypto_sign_PUBLICKEYBYTES)
    return;
  std::memcpy(args->key, rawKey.data(), crypto_sign_PUBLICKEYBYTES);
  thread_id t = spawn_thread(Habitat::initiate__, "New Connection", 0, args);
  resume_thread(t);
}

int Habitat::accept__(void *args) {
  BDataIO *conn = ((AcceptArgs *)args)->link;
  std::function<void()> closeHook = ((AcceptArgs *)args)->closeHook;
  delete (AcceptArgs *)args;
  std::unique_ptr<BoxStream> shsPeer;
  try {
    shsPeer = std::make_unique<BoxStream>(
        std::unique_ptr<BDataIO>(conn), SSB_NETWORK_ID,
        static_cast<Habitat *>(be_app)->myId.get());
    auto rpc = new muxrpc::Connection(
        std::move(shsPeer), static_cast<Habitat *>(be_app)->clientMethods);
    if (closeHook)
      rpc->addCloseHook(closeHook);
    be_app->RegisterLooper(rpc);
    rpc->Run();
  } catch (HandshakeError err) {
    return -1;
  } catch (...) {
    delete conn;
    throw;
  }
  return 0;
}

int Habitat::initiate__(void *args) {
  try {
    int result =
        ((Habitat *)be_app)
            ->initiate__(std::unique_ptr<BDataIO>(((InitiateArgs *)args)->link),
                         ((InitiateArgs *)args)->key,
                         ((InitiateArgs *)args)->closeHook);
    delete (InitiateArgs *)args;
    return result;
  } catch (...) {
    return -1;
  }
}

int Habitat::initiate__(std::unique_ptr<BDataIO> link, unsigned char *key,
                        std::function<void()> closeHook) {
  auto sockptr = dynamic_cast<BSocket *>(link.get());
  // TODO: Check whether or not the "name" argument is still being used anywhere
  auto conn = new muxrpc::Connection(
      std::make_unique<BoxStream>(std::move(link), SSB_NETWORK_ID,
                                  this->myId.get(), key),
      this->clientMethods, "");
  if (sockptr)
    sockptr->SetTimeout(B_INFINITE_TIMEOUT);
  if (closeHook)
    conn->addCloseHook(closeHook);
  be_app->RegisterLooper(conn);
  conn->Run();
  return 0;
}

void Habitat::ReadyToRun() {
  this->databaseLooper->Run();
  this->lanBroadcaster = std::make_unique<LanBroadcaster>(this->myId->pubkey);
  this->loadSettings();
  this->AddHandler(this->lanBroadcaster.get());
  this->ipListener = std::make_unique<SSBListener>(
      this->myId, BMessenger(this->lanBroadcaster.get()), this->serverMethods);
  this->ipListener->run();
  this->ebt = new ebt::Dispatcher(this->databaseLooper);
  this->ebt->Run();
  this->RegisterLooper(this->ebt);
  {
    auto beginEBT = std::make_shared<ebt::Begin>(this->ebt);
    this->serverMethods.registerMethod(beginEBT);
    this->clientMethods = muxrpc::MethodSuite(this->serverMethods, false);
    this->clientMethods.copyHooks(this->serverMethods);
    this->clientMethods.registerConnectionHook(
        std::static_pointer_cast<muxrpc::ConnectionHook>(beginEBT));
  }
  rooms2::installClient(&this->clientMethods);
  this->worker = new TidyLooper("Worker thread");
  this->worker->Run();
  this->worker->Lock();
  BVolume volume;
  this->settings->GetVolume(&volume);
  while (!BMessenger(this->databaseLooper).IsValid() ||
         !BMessenger(this->contactStore).IsValid()) {
    snooze(500000);
  }
  auto graph = new ContactGraph(this->databaseLooper, this->contactStore);
  worker->AddHandler(graph);
  auto selector =
      new SelectContacts(BMessenger(this->databaseLooper), BMessenger(graph));
  worker->AddHandler(selector);
  BMessenger(selector).SendMessage('INIT');
  // Setup blobs
  {
    BDirectory blobsDir;
    if (this->settings->CreateDirectory("blobs", &blobsDir) == B_FILE_EXISTS) {
      BEntry entry;
      this->settings->FindEntry("blobs", &entry, true);
      blobsDir = BDirectory(&entry);
    }
    this->wantedBlobs = new blob::Wanted(blobsDir);
  }
  worker->AddHandler(this->wantedBlobs);
  this->wantedBlobs->registerMethods(this->serverMethods);
  worker->AddHandler(ConnectedList::instance());
  ConnectedList::instance()->addExcluded(this->myId->getCypherkey());
  worker->Unlock();
  this->RegisterLooper(worker);
  while (!BMessenger(this->contactStore).IsValid() ||
         !BMessenger(graph).IsValid()) {
    snooze(500000);
  }
  {
    BMessage getter(B_GET_PROPERTY);
    getter.AddSpecifier("Contact");
    BMessenger(this->contactStore).SendMessage(&getter, BMessenger(graph));
  }
  {
    BMessage rq(B_GET_PROPERTY);
    BMessage specifier('CPLX');
    specifier.AddString("property", "Post");
    specifier.AddString("type", "contact");
    specifier.AddBool("dregs", true);
    rq.AddSpecifier(&specifier);
    while (!BMessenger(graph).IsValid())
      snooze(500000);
    rq.AddMessenger("target", BMessenger(graph));
    BMessageRunner::StartSending(this->databaseLooper, &rq, 500000, 1);
  }
  this->checkServerStatus();
}

BMessenger Habitat::addWorker(BHandler *w) {
  auto worker = this->worker;
  if (!worker->Lock())
    return BMessenger();
  worker->AddHandler(w);
  BMessenger result(w);
  worker->Unlock();
  return result;
}

void Habitat::loadSettings() {
  if (BEntry entry; this->settings->FindEntry("preferences", &entry) == B_OK) {
    BMessage settings;
    {
      BFile file(&entry, B_READ_ONLY);
      if (settings.Unflatten(&file) != B_OK)
        return;
    }
    {
      int32 logCategory;
      for (int32 i = 0;
           settings.FindInt32("LogCategory", i, &logCategory) == B_OK; i++) {
        BMessage start(B_CREATE_PROPERTY);
        start.AddSpecifier("LogCategory");
        start.AddInt32("category", logCategory);
        BMessenger(this).SendMessage(&start);
      }
    }
    {
      BMessage record;
      for (int32 i = 0; settings.FindMessage("Server", i, &record) == B_OK; i++)
        this->servers.push_back(ServerRecord(&record));
    }
  }
}

void Habitat::saveSettings() {
  BMessage settings;
  for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
    if (Logger *logger = dynamic_cast<Logger *>(this->HandlerAt(i));
        logger != NULL) {
      logger->storeCategories(&settings);
    }
  }
  for (auto &server : this->servers) {
    BMessage record;
    server.pack(&record, false);
    settings.AddMessage("Server", &record);
  }
  BFile output;
  if (this->settings->CreateFile("preferences~", &output, false) != B_OK)
    return;
  if (settings.Flatten(&output) == B_OK) {
    BEntry entry;
    if (this->settings->FindEntry("preferences~", &entry) == B_OK)
      entry.Rename("preferences", true);
  }
}

void Habitat::Quit() {
  this->saveSettings();
  this->ipListener->halt();
  this->StopWatchingAll(BMessenger(this->databaseLooper));
  // Avoid race condition that can happen in `BApplication::Quit` by waiting
  // for other loopers to finish first.
  std::set<thread_id> lt;
  for (int32 i = this->CountLoopers() - 1; i > 0;) {
    auto l = this->LooperAt(i);
    this->Unlock();
    if (l != this && l->Lock()) {
      lt.insert(l->Thread());
      BMessenger(l).SendMessage(B_QUIT_REQUESTED);
      l->Unlock();
      this->Lock();
      this->UnregisterLooper(l);
    } else {
      this->Lock();
    }
    i--;
  }
  this->Unlock();
  for (auto t : lt) {
    status_t exitValue;
    wait_for_thread(t, &exitValue);
  }
  this->Lock();
  BApplication::Quit();
}

BDirectory &Habitat::settingsDir() { return *this->settings; }

static BRect initialFrame() {
  auto ss = BScreen().Frame();
  if (ss.right > 512)
    ss.right = 512;
  ss.top = 25;
  ss.InsetBy(5, 5);
  return ss;
}

MainWindow::MainWindow(SSBDatabase *db)
    :
    BWindow(initialFrame(), "Habitat", B_DOCUMENT_WINDOW,
            B_QUIT_ON_WINDOW_CLOSE, B_CURRENT_WORKSPACE) {
  BRect mbarRect(this->Bounds());
  mbarRect.bottom = 20;
  this->menuBar = new BMenuBar(mbarRect, "menubar");
  BMenu *appMenu = new BMenu(B_TRANSLATE("Application"));
  appMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Settings"), new BMessage('PRFS')));
  this->menuBar->AddItem(appMenu);
  this->AddChild(this->menuBar);
  BRect statusRect(this->Bounds());
  statusRect.top = statusRect.bottom - 20;
  // TODO: Find a better way to do this than BStatusBar
  this->statusBar = new BStatusBar(statusRect, "");
  this->AddChild(this->statusBar);
  db->StartWatching(this, 'BKLG');
}

void MainWindow::MessageReceived(BMessage *message) {
  switch (message->what) {
  case 'PRFS': {
    SettingsWindow *window = new SettingsWindow();
    window->Show();
  } break;
  case B_OBSERVER_NOTICE_CHANGE:
    if (uint64 backlog; message->FindUInt64("backlog", &backlog) == B_OK) {
      // TODO:
      //   Refactor to allow for multiple details
      //   Hide when the number is 0
      BString numstring;
      numstring << backlog;
      BString status = BString(B_TRANSLATE("{} unindexed messages"))
                           .Replace("{}", numstring, 1);
      this->statusBar->SetText(status);
    }
    break;
  default:
    BWindow::MessageReceived(message);
  }
}

ServerRecord::ServerRecord() {}

ServerRecord::ServerRecord(const BString &transport, const BString &hostname,
                           const BString &cypherkey)
    :
    transport(transport),
    hostname(hostname),
    cypherkey(cypherkey) {}

ServerRecord::ServerRecord(const BString &hostname, const BString &cypherkey)
    :
    transport("net"),
    hostname(hostname),
    cypherkey(cypherkey) {}

ServerRecord::ServerRecord(BMessage *record)
    :
    transport(record->GetString("transport", "net")),
    hostname(record->GetString("hostname", "")),
    cypherkey(record->GetString("cypherkey", "")),
    connected(record->GetBool("connected", false)) {}

bool ServerRecord::isValid() {
  return this->transport == "net" &&
      validateHostname(this->hostname, PORT_REQUIRED) &&
      validateCypherkey(this->cypherkey);
}

void ServerRecord::pack(BMessage *record, bool includeStatus) {
  record->AddString("transport", this->transport);
  record->AddString("hostname", this->hostname);
  record->AddString("cypherkey", this->cypherkey);
  if (includeStatus)
    record->AddBool("connected", this->connected);
}

status_t ServerRecord::update(const BMessage *record) {
  BString transport, hostname, cypherkey;
  bool setTransport = false, setHostname = false, setCypherkey = false;
  if (record->FindString("transport", &transport) == B_OK) {
    // TODO: Support other transports
    if (transport == "net")
      setTransport = true;
    else
      return B_BAD_VALUE;
  }
  if (record->FindString("hostname", &hostname) == B_OK) {
    // TODO: Make validation rules dependent on transport
    if (validateHostname(hostname, PORT_REQUIRED))
      setHostname = true;
    else
      return B_BAD_VALUE;
  }
  if (record->FindString("cypherkey", &cypherkey) == B_OK) {
    if (validateCypherkey(cypherkey))
      setCypherkey = true;
    else
      return B_BAD_VALUE;
  }
  if (bool value; record->FindBool("connected", &value) == B_OK)
    this->connected = value;
  if (setTransport)
    this->transport = transport;
  if (setHostname)
    this->hostname = hostname;
  if (setCypherkey)
    this->cypherkey = cypherkey;
  return B_OK;
}

BString ServerRecord::fullName() {
  BString result = this->transport;
  result << ":";
  result << this->hostname;
  result << "~shs:";
  BString chunk;
  this->cypherkey.CopyInto(chunk, 1, this->cypherkey.Length() - 9);
  result << chunk;
  return result;
}

void ServerRecord::connect() {
  BMessage trigger(B_CREATE_PROPERTY);
  {
    BString justHost;
    int16 port;
    int32 separator = this->hostname.FindLast(':');
    this->hostname.CopyInto(justHost, 0, separator);
    port = std::stoi(&this->hostname.String()[separator + 1]);
    trigger.AddString("host", justHost);
    trigger.AddInt16("port", port);
  }
  {
    BString key;
    this->cypherkey.CopyInto(key, 1, 44);
    trigger.AddString("key", key);
  }
  trigger.AddString("name", this->fullName());
  trigger.AddSpecifier("Connection");
  BMessenger(be_app).SendMessage(&trigger);
  this->connected = true;
}

namespace {

TidyLooper::TidyLooper(const char *name)
    :
    BLooper(name) {}

void TidyLooper::Quit() {
  for (int32 i = this->CountHandlers() - 1; i >= 0; i--) {
    auto h = this->HandlerAt(i);
    if (!dynamic_cast<BLooper *>(h)) {
      delete h;
      this->RemoveHandler(h);
    }
  }
  BLooper::Quit();
}
} // namespace

#undef B_TRANSLATION_CONTEXT
