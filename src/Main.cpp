#define MAIN_CPP
#include "Main.h"
#include <Catalog.h>
#include <FindDirectory.h>
#include <PropertyInfo.h>
#include <iostream>
#include <sodium.h>

#define B_TRANSLATION_CONTEXT "MainWindow"

Habitat *app;

int main(int argc, const char **args) {
  int exit_status = 0;
  if (sodium_init() == -1) {
    std::cerr << B_TRANSLATE("Failed to initialize libsodium") << '\n';
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

static property_info habitatProperties[] = {
    {"Timezone",
     {B_GET_PROPERTY, B_SET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "The time zone used for date and time operations",
     0,
     {B_STRING_TYPE}},
    {0}};

Habitat::Habitat(void)
    :
    BApplication("application/x-vnd.habitat") {

  this->mainWindow = new MainWindow();
  this->mainWindow->Show();
  BPath settings_path;
  find_directory(B_USER_SETTINGS_DIRECTORY, &settings_path, true);
  BDirectory settings_parent = BDirectory(settings_path.Path());
  BDirectory settings;
  status_t status = settings_parent.CreateDirectory("Habitat", &settings);
  if (status == B_FILE_EXISTS) {
    BEntry entry;
    status = settings_parent.FindEntry("Habitat", &entry, false);
    if (status != B_OK)
      throw status;
    this->settings = std::unique_ptr<BDirectory>(new BDirectory(&entry));
  } else if (status == B_OK) {
    this->settings = std::unique_ptr<BDirectory>(new BDirectory(settings));
  } else {
    throw status;
  }
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
  if (propertyInfo.FindMatch(msg, index, specifier, what, property) >= 0)
    return this;
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
  if (msg->GetCurrentSpecifier(&index, &specifier, &what, &property) != B_OK)
    return BApplication::MessageReceived(msg);
  BPropertyInfo propertyInfo(habitatProperties);
  switch (propertyInfo.FindMatch(msg, index, &specifier, what, property)) {
  case 0: // Timezone
    if (msg->what == B_SET_PROPERTY) {
      BString tz;
      if (msg->FindString("data", &tz) == B_OK) {
        // TODO
        error = B_OK;
      }
    } else if (msg->what == B_GET_PROPERTY) {
      reply.AddString("result", "Pacific/Auckland");
      error = B_OK;
    }
    break;
  default:
    return BApplication::MessageReceived(msg);
  }
  reply.AddInt32("error", error);
  msg->SendReply(&reply);
}

MainWindow::MainWindow(void)
    :
    BWindow(BRect(100, 100, 520, 400), "Habitat", B_DOCUMENT_WINDOW,
            B_QUIT_ON_WINDOW_CLOSE, B_CURRENT_WORKSPACE) {}

#undef B_TRANSLATION_CONTEXT
