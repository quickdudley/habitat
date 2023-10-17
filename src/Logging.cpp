#include "Logging.h"
#include <Application.h>
#include <TimeFormat.h>
#include <ctime>
#include <iostream>

void writeLog(int32 category, BString &text) {
  BMessage logMessage('LOG_');
  logMessage.AddInt32("category", category);
  logMessage.AddString("text", text);
  be_app.Lock();
  for (int32 i = be_app->CountHandlers() - 1; i >= 0; i--) {
  	if (Logger *logger = dynamic_cast<Logger *>(be_app->HandlerAt(i)); logger != NULL) {
	  BMessenger(logger).SendMessage(&logMessage);
  	}
  }
  be_app.Unlock();
}

Logger::Logger() {}

void Logger::MessageReceived(BMessage *message) {
  switch (message->what) {
  case 'LOG_': {
    time_t now = time(NULL);
    if (int32 category;
        message->FindInt32("category", &category) == B_OK &&
        this->categories.find(category) != this->categories.end()) {
      if (BString text; message->FindString("text", &text) == B_OK) {
        BString line("[");
        {
          BString formattedTime;
          BTimeFormat().Format(formattedTime, now,
                                  B_FULL_TIME_FORMAT);
          line << formattedTime;
        }
        line << "] {";
        line.Append((char *)&category, sizeof(int32));
        line << "}: ";
        line << text;
        if (this->output == nullptr)
          std::cerr << line.String() << std::endl;
        else {
          line << "\n";
          this->output->WriteExactly(line.String(), line.Length());
        }
      }
    }
  } break;
  default:
    return BHandler::MessageReceived(message);
  }
}

void Logger::enableCategory(int32 category) {
  this->categories.insert(category);
}

void Logger::disableCategory(int32 category) {
  this->categories.erase(category);
}