#include "SettingsWindow.h"
#include <Button.h>
#include <Catalog.h>
#include <GroupLayout.h>
#include <ListView.h>
#include <LocaleRoster.h>
#include <Screen.h>
#include <sodium.h>

#define B_TRANSLATION_CONTEXT "Settings"

#define WIDTH 400
#define HEIGHT 400

namespace {
class NetworkTab : public BView {
public:
  NetworkTab(BRect contentFrame);

private:
  BListView *serverList;
  BButton *addButton;
  BButton *delButton;
};

NetworkTab::NetworkTab(BRect contentFrame)
    :
    BView(contentFrame, B_TRANSLATE("Network"), B_FOLLOW_ALL_SIDES,
          B_WILL_DRAW) {
  this->AdoptSystemColors();
  auto outerLayout = new BGroupLayout(B_HORIZONTAL);
  this->SetLayout(outerLayout);
  {
    auto column1Layout = new BGroupLayout(B_VERTICAL);
    outerLayout->AddItem(column1Layout);
    this->serverList = new BListView("ServerList");
    column1Layout->AddView(this->serverList);
    {
      auto buttonLayout = new BGroupLayout(B_HORIZONTAL);
      column1Layout->AddItem(buttonLayout, 0.2);
      this->addButton = new BButton(B_TRANSLATE("Add"), new BMessage('ASRV'));
      buttonLayout->AddView(this->addButton);
      this->delButton =
          new BButton(B_TRANSLATE("Remove"), new BMessage('DSRV'));
      buttonLayout->AddView(this->delButton);
    }
  }
}
}; // namespace

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
  BView *networkTab = new NetworkTab(contentFrame);
  this->tabView->AddTab(networkTab);
}

SettingsWindow::~SettingsWindow() {}

#undef B_TRANSLATION_CONTEXT
