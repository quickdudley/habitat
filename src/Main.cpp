#include "Main.h"
#include "Base64.h"
#include "Connection.h"
#include "ContactGraph.h"
#include "Indices.h"
#include "Logging.h"
#include "MigrateDB.h"
#include "SelectContacts.h"
#include "SettingsWindow.h"
#include <ByteOrder.h>
#include <Catalog.h>
#include <File.h>
#include <FindDirectory.h>
#include <LocaleRoster.h>
#include <MenuItem.h>
#include <PropertyInfo.h>
#include <TimeZone.h>
#include <iostream>
#include <signal.h>
#include <sodium.h>
#include <string>

#define B_TRANSLATION_CONTEXT "MainWindow"

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
  delete app;
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
     {B_DELETE_PROPERTY, B_GET_PROPERTY, 0},
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
    database = migrateToSqlite(*this->settings);
    // Create contacts directory
    status = this->settings->CreateDirectory("contacts", &contactsDir);
    if (status == B_FILE_EXISTS) {
      BEntry entry;
      status = this->settings->FindEntry("contacts", &entry, true);
      if (status != B_OK)
        throw status;
      contactsDir = BDirectory(&entry);
    } else if (status != B_OK) {
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
  this->databaseLooper = new SSBDatabase(database);
  this->ownFeed = new OwnFeed(database, this->myId.get());
  this->databaseLooper->AddHandler(this->ownFeed);
  this->ownFeed->load();
  this->RegisterLooper(databaseLooper);
  // Open main window
  this->mainWindow = new MainWindow();
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
    msg->PrintToStream();
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
      this->servers.push_back(msg);
      if (!this->servers.back().isValid())
        this->servers.pop_back();
      else
        error = B_OK;
    } break;
    case B_DELETE_PROPERTY: {
      error = B_NAME_NOT_FOUND;
      for (auto i = this->servers.begin(); i != this->servers.end(); i++) {
        if (i->hostname == specifier.GetString("name")) {
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
            server.hostname == specifier.GetString("name")) {
          BMessage result;
          server.pack(&result);
          reply.AddMessage("result", &result);
          error = B_OK;
        }
      }
    } break;
    }
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
  if (msg->IsSourceWaiting())
    msg->SendReply(&reply);
  if (detached)
    delete msg;
}

thread_id Habitat::Run() {
  thread_id r = this->databaseLooper->Run();
  this->lanBroadcaster = std::make_unique<LanBroadcaster>(this->myId->pubkey);
  BApplication::Run();
  return r;
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
    if (rawKey.size() != 32) {
      error = B_BAD_VALUE;
      goto sendReply;
    }
    try {
      auto conn = new muxrpc::Connection(
          std::make_unique<BoxStream>(
              std::make_unique<BSocket>(BNetworkAddress(host, port)),
              SSB_NETWORK_ID, static_cast<Habitat *>(be_app)->myId.get(),
              rawKey.data()),
          static_cast<Habitat *>(be_app)->clientMethods);
      be_app->RegisterLooper(conn);
      conn->Run();
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
  if (msg->IsSourceWaiting())
    msg->SendReply(&reply);
  delete msg;
  // TODO: Reap thread ID
  return 0;
}

void Habitat::ReadyToRun() {
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
  auto worker = new BLooper("Worker thread");
  worker->Run();
  worker->Lock();
  BVolume volume;
  this->settings->GetVolume(&volume);
  auto graph = new ContactGraph();
  worker->AddHandler(graph);
  {
    BMessage rq(B_GET_PROPERTY);
    BMessage specifier('CPLX');
    specifier.AddString("property", "Post");
    specifier.AddString("type", "contact");
    specifier.AddBool("dregs", true);
    rq.AddSpecifier(&specifier);
    rq.AddMessenger("target", BMessenger(graph));
    BMessenger(this->databaseLooper).SendMessage(&rq);
  }
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
  worker->Unlock();
  this->RegisterLooper(worker);
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
  BApplication::Quit();
}

BDirectory &Habitat::settingsDir() { return *this->settings; }

MainWindow::MainWindow(void)
    :
    BWindow(BRect(100, 100, 520, 400), "Habitat", B_DOCUMENT_WINDOW,
            B_QUIT_ON_WINDOW_CLOSE, B_CURRENT_WORKSPACE) {
  BRect mbarRect(this->Bounds());
  mbarRect.bottom = 20;
  this->menuBar = new BMenuBar(mbarRect, "menubar");
  BMenu *appMenu = new BMenu(B_TRANSLATE("Application"));
  appMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Settings"), new BMessage('PRFS')));
  this->menuBar->AddItem(appMenu);
  this->AddChild(this->menuBar);
}

void MainWindow::MessageReceived(BMessage *message) {
  switch (message->what) {
  case 'PRFS': {
    SettingsWindow *window = new SettingsWindow();
    window->Show();
  } break;
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
    transport(record->GetString("transport", "")),
    hostname(record->GetString("hostname", "")),
    cypherkey(record->GetString("cypherkey", "")) {}

bool ServerRecord::isValid() {
  return this->transport == "net" &&
      validateHostname(this->hostname, PORT_REQUIRED) &&
      validateCypherkey(this->cypherkey);
}

void ServerRecord::pack(BMessage *record) {
  record->AddString("transport", this->transport);
  record->AddString("hostname", this->hostname);
  record->AddString("cypherkey", this->cypherkey);
}

#undef B_TRANSLATION_CONTEXT
