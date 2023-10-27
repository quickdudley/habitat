#include "Main.h"
#include "Indices.h"
#include "Logging.h"
#include <ByteOrder.h>
#include <Catalog.h>
#include <File.h>
#include <FindDirectory.h>
#include <LocaleRoster.h>
#include <MenuItem.h>
#include <PropertyInfo.h>
#include <TimeZone.h>
#include <iostream>
#include <sodium.h>
#include <string>

#define B_TRANSLATION_CONTEXT "MainWindow"

Habitat *app;

int main(int argc, const char **args) {
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

enum { kTimeZone, kCypherkey, kCreateBlob, kCreatePost, kLogCategory };

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
     {B_DIRECT_SPECIFIER, 0},
     "An enabled category of log entries",
     kLogCategory,
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
    // Create posts directory
    status = this->settings->CreateDirectory("posts", this->postDir.get());
    if (status == B_FILE_EXISTS) {
      BEntry entry;
      status = this->settings->FindEntry("posts", &entry, true);
      if (status != B_OK)
        throw status;
      this->postDir = std::make_unique<BDirectory>(&entry);
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
  this->databaseLooper = new SSBDatabase(*this->postDir);
  this->ownFeed = new OwnFeed(*this->postDir, this->myId.get());
  this->databaseLooper->AddHandler(this->ownFeed);
  this->ownFeed->load();
  this->RegisterLooper(databaseLooper);
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
  this->databaseLooper->AddHandler(this->wantedBlobs);
  this->wantedBlobs->registerMethods();
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
  if (!msg->HasSpecifiers()) {
    if (msg->what == 'LOG_') {
      for (int32 i = be_app->CountHandlers() - 1; i >= 0; i--) {
        if (Logger *logger = dynamic_cast<Logger *>(this->HandlerAt(i));
            logger != NULL) {
          BMessenger(logger).SendMessage(msg);
        }
      }
      return;
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
    }
    error = B_OK;
    msg->PrintToStream();
    break;
  case kLogCategory: {
    int32 category;
    if (BString cascii; msg->FindString("category", &cascii) == B_OK) {
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
  default:
    return BApplication::MessageReceived(msg);
  }
  reply.AddInt32("error", error);
  if (error != B_OK)
    reply.AddString("message", strerror(error));
  if (msg->IsSourceWaiting())
    msg->SendReply(&reply);
}

thread_id Habitat::Run() {
  thread_id r = this->databaseLooper->Run();
  this->lanBroadcaster = std::make_unique<LanBroadcaster>(this->myId->pubkey);
  BApplication::Run();
  return r;
}

void Habitat::ReadyToRun() {
  this->loadSettings();
  this->AddHandler(this->lanBroadcaster.get());
  this->ipListener = std::make_unique<SSBListener>(
      this->myId, BMessenger(this->lanBroadcaster.get()));
  this->ipListener->run();
  this->ebt = new ebt::Dispatcher(this->databaseLooper);
  this->ebt->Run();
  this->RegisterLooper(this->ebt);
  registerMethod(std::make_shared<ebt::Begin>(this->ebt));
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

#undef B_TRANSLATION_CONTEXT
