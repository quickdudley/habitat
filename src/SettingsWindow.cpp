#include "SettingsWindow.h"
#include "Base64.h"
#include "Connection.h"
#include "Invite.h"
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <ListView.h>
#include <LocaleRoster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <TextControl.h>
#include <sodium.h>

#define B_TRANSLATION_CONTEXT "Settings"

#define WIDTH 480
#define HEIGHT 400

namespace {
class NetworkTab : public BView {
public:
  NetworkTab(BRect contentFrame);
  void AttachedToWindow();
  void MessageReceived(BMessage *msg) override;
  void clearDetails();
  void fillDetails(const BString &netAddress, const BString &cypherkey);

private:
  BListView *serverList;
  BTextControl *addrControl;
  BTextControl *keyControl;
  BButton *addButton;
  BButton *delButton;
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
  BString getName();

private:
  BString netAddress;
  BString cypherkey;
};

class ServerList : public BListView {
public:
  ServerList(const char *name, NetworkTab *container);
  void SelectionChanged() override;

private:
  NetworkTab *container;
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
    this->serverList = new ServerList("ServerList", this);
    column1Layout->AddView(new BScrollView(
        NULL, this->serverList, B_WILL_DRAW | B_SUPPORTS_LAYOUT, false, true));
    {
      auto buttonLayout = new BGroupLayout(B_HORIZONTAL);
      column1Layout->AddItem(buttonLayout, 0.2);
      this->addButton = new BButton(B_TRANSLATE("New"), new BMessage('ASRV'));
      this->addButton->SetTarget(this);
      buttonLayout->AddView(this->addButton);
      this->delButton =
          new BButton(B_TRANSLATE("Remove"), new BMessage('DSRV'));
      this->delButton->SetTarget(this);
      this->delButton->SetEnabled(false);
      buttonLayout->AddView(this->delButton);
    }
  }
  outerLayout->AddView(new BSeparatorView(B_VERTICAL));
  {
    auto column2Layout = new BGroupLayout(B_VERTICAL);
    outerLayout->AddItem(column2Layout);
    this->addrControl = new BTextControl(B_TRANSLATE("Network address"), "",
                                         new BMessage('NADR'));
    this->addrControl->SetTarget(this);
    this->addrControl->SetModificationMessage(new BMessage('MSRV'));
    column2Layout->AddView(this->addrControl);
    this->keyControl =
        new BTextControl(B_TRANSLATE("Public key"), "", new BMessage('CKEY'));
    this->keyControl->SetTarget(this);
    this->keyControl->SetModificationMessage(new BMessage('MSRV'));
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
  {
    BMessage rq(B_GET_PROPERTY);
    rq.AddSpecifier("Server");
    BMessenger(be_app).SendMessage(&rq, BMessenger(this));
  }
  be_clipboard->StartWatching(this);
}

void NetworkTab::MessageReceived(BMessage *message) {
  switch (message->what) {
  case 'ASRV': {
    this->serverList->DeselectAll();
    if (be_clipboard->Lock()) {
      const char *clip;
      ssize_t cliplen;
      be_clipboard->Data()->FindData("text/plain", B_MIME_TYPE,
                                     (const void **)&clip, &cliplen);
      be_clipboard->Unlock();
      BMessage parsed;
      if (invite::parse(&parsed, clip) == B_OK) {
        this->addrControl->SetText(parsed.GetString("hostname", ""));
        this->keyControl->SetText(parsed.GetString("cypherkey", ""));
      }
    }
  } break;
  case 'DSRV': {
    if (auto item = dynamic_cast<ServerEntry *>(
            this->serverList->ItemAt(this->serverList->CurrentSelection()))) {
      BMessage msg(B_DELETE_PROPERTY);
      msg.AddSpecifier("Server", item->getName());
      BMessenger(be_app).SendMessage(&msg);
    }
    this->serverList->RemoveItem(this->serverList->CurrentSelection());
    this->serverList->DeselectAll();
  } break;
  case 'SSRV': {
    if (auto item = dynamic_cast<ServerEntry *>(
            this->serverList->ItemAt(this->serverList->CurrentSelection()))) {
      BString name = item->getName();
      item->setAddress(this->addrControl->Text());
      item->setCypherkey(this->keyControl->Text());
      BMessage update(B_SET_PROPERTY);
      update.AddString("hostname", item->getAddress());
      update.AddString("cypherkey", item->getCypherkey());
      update.AddSpecifier("Server", name.String());
      BMessenger(be_app).SendMessage(&update);
      this->serverList->Invalidate();
    } else {
      int32 ll = this->serverList->CountItems();
      auto newItem =
          new ServerEntry(this->addrControl->Text(), this->keyControl->Text());
      this->serverList->AddItem(newItem);
      BMessage create(B_CREATE_PROPERTY);
      create.AddString("hostname", newItem->getAddress());
      create.AddString("cypherkey", newItem->getCypherkey());
      create.AddSpecifier("Server");
      this->serverList->Select(ll);
      BMessenger(be_app).SendMessage(&create);
    }
  } break;
  case 'MSRV': {
    this->saveButton->SetEnabled(
        validateHostname(this->addrControl->Text(), PORT_REQUIRED) &&
        validateCypherkey(this->keyControl->Text()));
  } break;
  case B_REPLY: {
    int32 i = 0;
    BMessage result;
    while (message->FindMessage("result", i, &result) == B_OK) {
      if (BString(result.GetString("transport", "net")) == "net") {
        this->serverList->AddItem(
            new ServerEntry(result.GetString("hostname", ""),
                            result.GetString("cypherkey", "")));
        i++;
      }
    }
    // TODO: populate the list
  } break;
  default:
    BView::MessageReceived(message);
  }
}

void NetworkTab::clearDetails() {
  this->addrControl->SetText("");
  this->keyControl->SetText("");
  this->delButton->SetEnabled(false);
}

void NetworkTab::fillDetails(const BString &netAddress,
                             const BString &cypherkey) {
  this->addrControl->SetText(netAddress);
  this->keyControl->SetText(cypherkey);
  this->delButton->SetEnabled(true);
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

BString ServerEntry::getName() {
  BString result("net:");
  result << this->getAddress();
  result << "~shs:";
  BString key;
  this->getCypherkey().CopyInto(key, 1, 44);
  result << key;
  return result;
}

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

ServerList::ServerList(const char *name, NetworkTab *container)
    :
    BListView(name),
    container(container) {}

void ServerList::SelectionChanged() {
  if (auto selected =
          dynamic_cast<ServerEntry *>(this->ItemAt(this->CurrentSelection()))) {
    this->container->fillDetails(selected->getAddress(),
                                 selected->getCypherkey());
  } else {
    this->container->clearDetails();
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
