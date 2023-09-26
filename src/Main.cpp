#include "Main.h"
#include "Indices.h"
#include <Catalog.h>
#include <File.h>
#include <FindDirectory.h>
#include <LocaleRoster.h>
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

enum { kTimeZone, kCypherkey, kCreatePost };

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
    {0}};

Habitat::Habitat(void)
    :
    BApplication("application/x-vnd.habitat") {
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
    BEntry secret;
    status = this->settings->FindEntry("secret", &secret, true);
    if (status == B_OK) {
      BPath path;
      secret.GetPath(&path);
      BFile secretFile(&secret, B_READ_ONLY);
      char buffer[1024];
      JSON::Parser parser(std::make_unique<JSON::SecretNode>(&this->myId));
      ssize_t readBytes;
      while (readBytes = secretFile.Read(buffer, 1024), readBytes > 0) {
        for (ssize_t i = 0; i < readBytes; i++) {
          parser.nextChar(buffer[i]);
        }
      }
    } else if (B_ENTRY_NOT_FOUND) {
      // Generate new secret
      BFile secretFile;
      status = this->settings->CreateFile("secret", &secretFile, true);
      if (status != B_OK)
        throw status;
      this->myId.generate();
      BString secretJson;
      JSON::RootSink sink(std::make_unique<JSON::SerializerStart>(&secretJson));
      this->myId.write(&sink);
      secretFile.WriteExactly(secretJson.String(), secretJson.Length(), NULL);
      secretFile.Sync();
    } else {
      throw status;
    }
  }
  // Create main feed looper
  this->databaseLooper = new SSBDatabase(*this->postDir);
  this->ownFeed = new OwnFeed(*this->postDir, &this->myId);
  this->databaseLooper->AddHandler(this->ownFeed);
  this->ownFeed->load();
  // Open main window
  this->mainWindow = new MainWindow();
  this->mainWindow->Show();
}

status_t Habitat::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat");
  BPropertyInfo propertyInfo(habitatProperties);
  data->AddFlat("messages", &propertyInfo);
  return BApplication::GetSupportedSuites(data);
}

BHandler *Habitat::ResolveSpecifier(BMessage *msg, int32 index,
                                    BMessage *specifier, int32 what,
                                    const char *property) {
  BPropertyInfo propertyInfo(habitatProperties);
  uint32 match;
  if (propertyInfo.FindMatch(msg, index, specifier, what, property, &match) >=
      0) {
    if (match == kCreatePost) {
      BMessenger(this->ownFeed).SendMessage(msg);
      return NULL;
    } else
      return this;
  }
  return BApplication::ResolveSpecifier(msg, index, specifier, what, property);
}

void Habitat::MessageReceived(BMessage *msg) {
  if (!msg->HasSpecifiers())
    return BApplication::MessageReceived(msg);
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
    reply.AddString("result", this->myId.getCypherkey());
    error = B_OK;
    break;
  default:
    return BApplication::MessageReceived(msg);
  }
  reply.AddInt32("error", error);
  if (error != B_OK)
    reply.AddString("message", strerror(error));
  msg->SendReply(&reply);
}

thread_id Habitat::Run() {
  thread_id r = this->databaseLooper->Run();
  this->broadcastArgs.ssbPort = 8008; // TODO make dynimic
  memcpy(this->broadcastArgs.pubkey, this->myId.pubkey,
         crypto_sign_PUBLICKEYBYTES);
  {
    BNetworkAddress local;
    local.SetToWildcard(AF_INET, 8008);
    this->broadcastArgs.socket.Bind(local, true);
    this->broadcastArgs.socket.SetBroadcast(true);
  }
  this->broadcastThreadId = runBroadcastThread(&(this->broadcastArgs));
  BApplication::Run();
  return r;
}

void Habitat::Quit() {
  send_data(this->broadcastThreadId, 'QUIT', NULL, 0);
  status_t exitValue;
  wait_for_thread(this->broadcastThreadId, &exitValue);
  BApplication::Quit();
}

MainWindow::MainWindow(void)
    :
    BWindow(BRect(100, 100, 520, 400), "Habitat", B_DOCUMENT_WINDOW,
            B_QUIT_ON_WINDOW_CLOSE, B_CURRENT_WORKSPACE) {}

#undef B_TRANSLATION_CONTEXT
