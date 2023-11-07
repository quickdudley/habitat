#include "SettingsWindow.h"
#include <Catalog.h>
#include <LocaleRoster.h>
#include <Screen.h>
#include <sodium.h>

#define B_TRANSLATION_CONTEXT "Settings"

#define WIDTH 400
#define HEIGHT 400

static inline BRect ourFrame() {
  BRect screenFrame = BScreen().Frame();
  BPoint corner(
      (float)randombytes_uniform((uint32)screenFrame.Width() - WIDTH),
      (float)randombytes_uniform((uint32)screenFrame.Height() - HEIGHT - 20) +
          20);
  return BRect(corner, BSize(WIDTH, HEIGHT));
}

SettingsWindow::SettingsWindow()
    :
    BWindow(ourFrame(), B_TRANSLATE("Habitat Settings"), B_UNTYPED_WINDOW, 0,
            B_CURRENT_WORKSPACE) {
  this->tabView = new BTabView(this->Bounds(), "settingsTabView");
  this->AddChild(tabView);
  BRect contentFrame = this->tabView->ContainerView()->Bounds();
  BView *networkTab = new BView(contentFrame, B_TRANSLATE("Network"),
                                B_FOLLOW_ALL_SIDES, B_WILL_DRAW);
  this->tabView->AddTab(networkTab);
}

SettingsWindow::~SettingsWindow() {}

#undef B_TRANSLATION_CONTEXT
