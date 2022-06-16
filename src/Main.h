#ifndef MAIN_H
#define MAIN_H

#include <Application.h>
#include <Directory.h>
#include <Path.h>
#include <Window.h>
#include <memory>

class MainWindow : public BWindow {
public:
  MainWindow(void);
};

class Habitat : public BApplication {
public:
  Habitat(void);

private:
  MainWindow *mainWindow;
  std::unique_ptr<BDirectory> settings;
};

#endif
