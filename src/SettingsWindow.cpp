#include "SettingsWindow.h"
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <ListView.h>
#include <LocaleRoster.h>
#include <Screen.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <TextControl.h>
#include <sodium.h>

#define B_TRANSLATION_CONTEXT "Settings"

#define WIDTH 400
#define HEIGHT 400

namespace {
class NetworkTab : public BView {
public:
  NetworkTab(BRect contentFrame);
  void AttachedToWindow();
  void MessageReceived(BMessage *msg) override;

private:
  BListView *serverList;
  BTextControl *addrControl;
  BTextControl *keyControl;
  BButton *saveButton;
};

class ServerEntry : public BListItem {
public:
  ServerEntry(const BString &netAddress = "", const BString cypherkey = "");
  void setAddress(const BString &netAddress);
  void setCypherkey(const BString &cypherkey);
  void DrawItem(BView *owner, BRect frame, bool complete = false) override;
  void Update(BView *owner, const BFont *font) override;
  const BString &getAddress();
  const BString &getCypherkey();

private:
  BString netAddress;
  BString cypherkey;
};

NetworkTab::NetworkTab(BRect contentFrame)
    :
    BView(contentFrame, B_TRANSLATE("Network"), B_FOLLOW_ALL_SIDES,
          B_WILL_DRAW) {
  this->AdoptSystemColors();
}

void NetworkTab::AttachedToWindow() {
  auto outerLayout = new BGroupLayout(B_HORIZONTAL);
  this->SetLayout(outerLayout);
  {
    auto column1Layout = new BGroupLayout(B_VERTICAL);
    outerLayout->AddItem(column1Layout);
    this->serverList = new BListView("ServerList");
    column1Layout->AddView(this->serverList);
    this->serverList->AddItem(new ServerEntry("testAddress", "testKey"), 0);
    {
      auto buttonLayout = new BGroupLayout(B_HORIZONTAL);
      column1Layout->AddItem(buttonLayout, 0.2);
      auto addButton = new BButton(B_TRANSLATE("Add"), new BMessage('ASRV'));
      addButton->SetTarget(this);
      buttonLayout->AddView(addButton);
      auto delButton = new BButton(B_TRANSLATE("Remove"), new BMessage('DSRV'));
      delButton->SetTarget(this);
      buttonLayout->AddView(delButton);
    }
  }
  outerLayout->AddView(new BSeparatorView(B_VERTICAL));
  {
    auto column2Layout = new BGroupLayout(B_VERTICAL);
    outerLayout->AddItem(column2Layout);
    this->addrControl = new BTextControl(B_TRANSLATE("Network address"), "",
                                         new BMessage('NADR'));
    this->addrControl->SetTarget(this);
    this->addrControl->SetEnabled(false);
    column2Layout->AddView(this->addrControl);
    this->keyControl =
        new BTextControl(B_TRANSLATE("Public key"), "", new BMessage('CKEY'));
    this->keyControl->SetEnabled(false);
    this->keyControl->SetTarget(this);
    column2Layout->AddView(this->keyControl);
    {
      auto buttonLayout = new BGroupLayout(B_HORIZONTAL);
      column2Layout->AddItem(buttonLayout);
      buttonLayout->AddItem(BSpaceLayoutItem::CreateVerticalStrut(1));
      this->saveButton = new BButton(B_TRANSLATE("Save"), new BMessage('SSRV'));
      this->saveButton->SetTarget(this);
      this->saveButton->SetEnabled(false);
      buttonLayout->AddView(saveButton);
    }
    column2Layout->AddItem(BSpaceLayoutItem::CreateGlue());
  }
}

void NetworkTab::MessageReceived(BMessage *message) {
  BView::MessageReceived(message);
}

ServerEntry::ServerEntry(const BString &netAddress, const BString cypherkey)
    :
    netAddress(netAddress),
    cypherkey(cypherkey) {}

void ServerEntry::setAddress(const BString &netAddress) {
  this->netAddress = netAddress;
}

void ServerEntry::setCypherkey(const BString &cypherkey) {
  this->cypherkey = cypherkey;
}

const BString &ServerEntry::getAddress() { return this->netAddress; }

const BString &ServerEntry::getCypherkey() { return this->cypherkey; }

void ServerEntry::DrawItem(BView *owner, BRect frame, bool complete) {
  rgb_color lowColor = owner->LowColor();
  rgb_color color = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
                                 : owner->ViewColor();
  owner->SetLowColor(color);
  owner->FillRect(frame, B_SOLID_LOW);
  font_height fontHeight;
  owner->GetFontHeight(&fontHeight);
  owner->MovePenTo(frame.left + be_control_look->DefaultLabelSpacing(),
                   frame.top + fontHeight.leading + fontHeight.ascent);
  owner->DrawString(this->netAddress);
  owner->MovePenTo(frame.left + be_control_look->DefaultLabelSpacing(),
                   frame.top + (fontHeight.leading + fontHeight.ascent) * 2 +
                       fontHeight.descent);
  owner->DrawString(this->cypherkey);
  owner->SetLowColor(lowColor);
}

void ServerEntry::Update(BView *owner, const BFont *font) {
  font_height fontHeight;
  font->GetHeight(&fontHeight);
  this->SetHeight(
      (fontHeight.ascent + fontHeight.descent + fontHeight.leading) * 2.1);
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
