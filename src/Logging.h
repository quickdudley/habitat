#ifndef LOGGING_H
#define LOGGING_H

#include <DataIO.h>
#include <Handler.h>
#include <memory>
#include <set>

void writeLog(int32 category, const BString &text);

class Logger : public BHandler {
public:
  Logger();
  void MessageReceived(BMessage *message) override;
  void enableCategory(int32 category);
  void disableCategory(int32 category);

private:
  std::set<int32> categories;
  std::unique_ptr<BDataIO> output;
};

#endif // LOGGING_H
