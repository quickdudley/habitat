#include "DatePage.h"
#include <algorithm>

RangeSet::RangeSet() {}

RangeSet::~RangeSet() {}

void RangeSet::set(int64 start, int64 end, bool value) {
  auto i1 = this->inner.upper_bound(start);
  auto i2 = this->inner.upper_bound(end);
  if (i1 != this->inner.begin()) {
  	i1--;
  	if (i1 != this->inner.end() && (start < i1->first || start - 1 > i1->second)) {
  	  i1 = this->inner.end();
  	}
  }
  if (i2 != this->inner.begin()) {
  	i2--;
  	if (i2 != this->inner.end() && (end < i2->first || start > i2->second)) {
  	  i2 = this->inner.end();
  	}
  }
  if (value) {
  	if (i1 != this->inner.end())
  	  start = std::min(start, i1->first);
  	if (i2 != this->inner.end())
  	  end = std::max(end, i2->second);
  }
}

bool RangeSet::operator[](int64 index) {
  if (this->inner.empty())
    return false;
  auto it = this->inner.upper_bound(index);
  if (it == this->inner.begin())
    return false;
  it--;
  return it->first <= index && it->second >= index;
}

const std::map<int64,int64> &RangeSet::ranges() {
  return this->inner;
}
