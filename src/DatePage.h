#ifndef DATE_PAGE_H
#define DATE_PAGE_H

#include <SupportDefs.h>
#include <map>

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

#endif // _H
