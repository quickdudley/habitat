#include "Main.h"
#include <Catalog.h>
#include <FindDirectory.h>
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

MainWindow::MainWindow(void)
    :
    BWindow(BRect(100, 100, 520, 400), "Habitat", B_DOCUMENT_WINDOW,
            B_QUIT_ON_WINDOW_CLOSE, B_CURRENT_WORKSPACE) {}

#undef B_TRANSLATION_CONTEXT
