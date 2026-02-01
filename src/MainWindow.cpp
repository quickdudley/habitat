#include "MainWindow.h"
#include "SettingsWindow.h"
#include <Catalog.h>
#include <GroupLayout.h>
#include <LocaleRoster.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <TimeZone.h>

#define B_TRANSLATION_CONTEXT "MainWindow"

static BRect initialFrame() {
  auto ss = BScreen().Frame();
  if (ss.right > 512)
    ss.right = 512;
  ss.top = 25;
  ss.InsetBy(5, 5);
  return ss;
}

MainWindow::MainWindow(SSBDatabase *db)
    : BWindow(initialFrame(), "Habitat", B_DOCUMENT_WINDOW,
              B_QUIT_ON_WINDOW_CLOSE, B_CURRENT_WORKSPACE) {
  // Set timezone
  {
    BTimeZone defaultTimeZone;
    BLocaleRoster::Default()->GetDefaultTimeZone(&defaultTimeZone);
    this->tz = U_ICU_NAMESPACE::TimeZone::createTimeZone(
        U_ICU_NAMESPACE::UnicodeString::fromUTF8(
            defaultTimeZone.ID().String()));
  }
  UErrorCode status = U_ZERO_ERROR;
  this->calendar =
      U_ICU_NAMESPACE::Calendar::createInstance(this->tz->clone(), status);
  this->menuBar = new BMenuBar("menubar");
  BMenu *appMenu = new BMenu(B_TRANSLATE("Application"));
  appMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Settings"), new BMessage('PRFS')));
  this->menuBar->AddItem(appMenu);
  auto mainLayout = new BGroupLayout(B_VERTICAL, 0);
  this->SetLayout(mainLayout);
  mainLayout->AddView(this->menuBar);
  // TODO: Extract to other method so we can switch date.
  BMessage spec('CPLX');
  auto cal =
      std::unique_ptr<U_ICU_NAMESPACE::Calendar>(this->calendar->clone());
  cal->set(UCAL_HOUR_OF_DAY, 0);
  cal->set(UCAL_MINUTE, 0);
  cal->set(UCAL_SECOND, 0);
  cal->set(UCAL_MILLISECOND, 0);
  UDate bod = cal->getTime(status);
  cal->set(UCAL_HOUR_OF_DAY, 23);
  cal->set(UCAL_MINUTE, 59);
  cal->set(UCAL_SECOND, 59);
  cal->set(UCAL_MILLISECOND, 999);
  UDate eod = cal->getTime(status);
  spec.AddInt64("earliest", bod);
  spec.AddInt64("latest", eod);
  this->feed = new FeedView(spec);
  this->contents = new BScrollView(
      NULL, this->feed, B_WILL_DRAW | B_SUPPORTS_LAYOUT, false, true);
  this->contents->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
  this->contents->SetExplicitPreferredSize(
      BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
  this->contents->SetExplicitMinSize(BSize(50.0f, 50.0f));
  mainLayout->AddView(this->contents);
  this->statusBar = new BStatusBar("");
  mainLayout->AddView(this->statusBar);
}

MainWindow::~MainWindow() {
  delete this->calendar;
  delete this->tz;
}

enum {
  kTimeZone,
};

static property_info properties[] = {
    {"Timezone",
     {B_GET_PROPERTY, B_SET_PROPERTY, 0},
     {B_DIRECT_SPECIFIER, 0},
     "The time zone used for date and time operations",
     kTimeZone,
     {B_STRING_TYPE}},
    {0}};

status_t MainWindow::GetSupportedSuites(BMessage *data) {
  data->AddString("suites", "suite/x-vnd.habitat+main-window");
  BPropertyInfo info(properties);
  data->AddFlat("messages", &info);
  return BWindow::GetSupportedSuites(data);
  return B_OK;
}

BHandler *MainWindow::ResolveSpecifier(BMessage *msg, int32 index,
                                       BMessage *specifier, int32 what,
                                       const char *property) {
  BPropertyInfo propertyInfo(properties);
  uint32 match;
  if (propertyInfo.FindMatch(msg, index, specifier, what, property, &match) >=
      0) {
    return this;
  } else {
    return BWindow::ResolveSpecifier(msg, index, specifier, what, property);
  }
}

void MainWindow::MessageReceived(BMessage *message) {
  {
    BMessage reply(B_REPLY);
    status_t error = B_ERROR;
    int32 index;
    BMessage specifier;
    int32 what;
    const char *property;
    uint32 match;
    if (message->GetCurrentSpecifier(&index, &specifier, &what, &property) < 0)
      goto notProp;
    BPropertyInfo propertyInfo(properties);
    propertyInfo.FindMatch(message, index, &specifier, what, property, &match);
    switch (match) {
    case kTimeZone:
      if (message->what == B_SET_PROPERTY) {
        BString tz;
        if (message->FindString("data", &tz) == B_OK) {
          U_ICU_NAMESPACE::TimeZone *utz =
              U_ICU_NAMESPACE::TimeZone::createTimeZone(
                  U_ICU_NAMESPACE::UnicodeString::fromUTF8(tz.String()));
          if (utz) {
            this->calendar->setTimeZone(*utz);
            delete this->tz;
            this->tz = utz;
            error = B_OK;
          }
        }
      } else if (message->what == B_GET_PROPERTY) {
        U_ICU_NAMESPACE::UnicodeString tzid;
        this->tz->getID(tzid);
        std::string tzidb;
        tzid.toUTF8String(tzidb);
        reply.AddString("result", tzidb.c_str());
        error = B_OK;
      }
      break;
    default:
      goto notProp;
    }
    reply.AddInt32("error", error);
    if (error != B_OK)
      reply.AddString("message", strerror(error));
    message->SendReply(&reply);
  }
notProp:
  switch (message->what) {
  case 'PRFS': {
    SettingsWindow *window = new SettingsWindow();
    window->Show();
  } break;
  case B_OBSERVER_NOTICE_CHANGE:
    if (uint64 backlog; message->FindUInt64("backlog", &backlog) == B_OK) {
      // TODO:
      //   Refactor to allow for multiple details
      //   Hide when the number is 0
      BString numstring;
      numstring << backlog;
      BString status = BString(B_TRANSLATE("{} unindexed messages"))
                           .Replace("{}", numstring, 1);
      this->statusBar->SetText(status);
    }
    break;
  default:
    BWindow::MessageReceived(message);
  }
}

#undef B_TRANSLATION_CONTEXT
