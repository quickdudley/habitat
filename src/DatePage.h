#ifndef DATE_PAGE_H
#define DATE_PAGE_H

#include <SupportDefs.h>
#include <View.h>
#include <map>
#include <unicode/timezone.h>
#include <vector>

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

class DayPager : public BView {
public:
  DayPager(const BMessage &specifier, U_ICU_NAMESPACE::TimeZone *tz, int32 year);
  ~DayPager() override;
  void setYear(int32 year);

private:
  BMessage specifier;
  std::vector<rgb_color> squares;
  U_ICU_NAMESPACE::TimeZone *tz;
  int32 year;
};

#endif // _H
