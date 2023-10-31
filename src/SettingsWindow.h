#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <TabView.h>
#include <Window.h>

class SettingsWindow : public BWindow {
public:
  SettingsWindow();
  ~SettingsWindow();

private:
  BTabView *tabView;
};

#endif
