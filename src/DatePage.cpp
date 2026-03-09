#include "DatePage.h"
#include <algorithm>
#include <vector>

RangeSet::RangeSet() {}

RangeSet::~RangeSet() {}

static inline bool contiguous(int64 s1, int64 e1, int64 s2, int64 e2) {
  if (s1 > s2) {
    int64 st = s2;
    int64 et = e2;
    s2 = s1;
    e2 = e1;
    s1 = st;
    e1 = et;
  }
  return e1 + 1 >= s2;
}

void RangeSet::set(int64 start, int64 end, bool value) {
  std::vector<std::pair<int64, int64>> q;
  std::map<int64, int64>::iterator ends[2] = {
      this->inner.upper_bound(start + 2), this->inner.upper_bound(end + 1)};
  for (auto &e : ends) {
    if (e != this->inner.begin())
      e--;
  }
  if (value) {
    if (ends[0] != this->inner.end() &&
        contiguous(start, end, ends[0]->first, ends[0]->second)) {
      start = std::min(start, ends[0]->first);
    }
    if (ends[1] != this->inner.end() &&
        contiguous(start, end, ends[1]->first, ends[1]->second)) {
      end = std::max(end, ends[1]->second);
    }
    q.push_back({start, end});
  } else {
    if (ends[0] != this->inner.end() &&
        contiguous(start, end, ends[0]->first, ends[0]->second)) {
      q.push_back({ends[0]->first, start - 1});
    }
    if (ends[1] != this->inner.end() &&
        contiguous(start, end, ends[1]->first, ends[1]->second)) {
      q.push_back({end + 1, ends[1]->second});
    }
  }
  if (ends[1] != this->inner.end())
    ends[1]++;
  for (auto i = ends[0]; i != ends[1] && i != this->inner.end();) {
    if (contiguous(start, end, i->first, i->second))
      i = this->inner.erase(i);
    else
      i++;
  }
  for (const auto &[a, b] : q) {
    if (b >= a)
      this->inner.insert({a, b});
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

const std::map<int64, int64> &RangeSet::ranges() { return this->inner; }
