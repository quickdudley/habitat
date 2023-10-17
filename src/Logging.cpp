#include "Logging.h"
#include <Application.h>
#include <TimeFormat.h>
#include <ctime>
#include <iostream>

void writeLog(int32 category, BString &text) {
  BMessage logMessage('LOG_');
  logMessage.AddUInt64("time", time(NULL));
  logMessage.AddInt32("category", category);
  logMessage.AddString("text", text);
  BMessenger(be_app).SendMessage(&logMessage);
}

Logger::Logger() {}

void Logger::MessageReceived(BMessage *message) {
  switch (message->what) {
  case 'LOG_': {
    time_t now = message->GetUInt64("time", time(NULL));
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