#ifndef DATE_PAGE_H
#define DATE_PAGE_H

#include <GroupView.h>
#include <SupportDefs.h>
#include <map>
#include <unicode/timezone.h>

class RangeSet {
public:
  RangeSet();
  virtual ~RangeSet();
  void set(int64 start, int64 end, bool value);
  bool operator[](int64 index);
  const std::map<int64,int64> &ranges();

private:
  std::map<int64,int64> inner;
};

class DayPager;

class DatePager : public BGroupView {
public:
  DatePager(const BMessage &specifier, U_ICU_NAMESPACE::TimeZone *tz, int32 year);
  ~DatePager() override;
  void AttachedToWindow() override;
  void MessageReceived(BMessage *msg) override;
  void populate();

private:
  BMessage specifier;
  U_ICU_NAMESPACE::TimeZone *tz;
  int32 year;
  RangeSet hasMessages;
  DayPager *dayPager;
friend class DayPager;
};

#endif // _H
