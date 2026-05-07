#include "DatePage.h"
#include <algorithm>
#include <unicode/calendar.h>
#include <unicode/gregocal.h>
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

static std::pair<int64, int64> yearBlock(U_ICU_NAMESPACE::TimeZone *tz,
                                         int32 year) {
  UErrorCode status = U_ZERO_ERROR;
  auto cal = std::unique_ptr<U_ICU_NAMESPACE::Calendar>(
      U_ICU_NAMESPACE::Calendar::createInstance(tz->clone(), status));
  cal->set(UCAL_YEAR, year);
  cal->set(UCAL_DAY_OF_YEAR, 1);
  cal->set(UCAL_MILLISECONDS_IN_DAY, 0);
  UDate boy = cal->getTime(status);
  cal->set(UCAL_YEAR, year + 1);
  return {(int64)boy, (int64)(cal->getTime(status) - 1)};
}

static std::pair<int64, int64> tsBlock(U_ICU_NAMESPACE::TimeZone *tz,
                                       int32 year, int64 timestamp) {
  UErrorCode status = U_ZERO_ERROR;
  auto cal = std::unique_ptr<U_ICU_NAMESPACE::Calendar>(
      U_ICU_NAMESPACE::Calendar::createInstance(tz->clone(), status));
  cal->setTime((UDate)timestamp, status);
  bool thisYear = cal->get(UCAL_YEAR, status) == year;
  if (!thisYear)
    cal->set(UCAL_DAY_OF_YEAR, 1);
  cal->set(UCAL_MILLISECONDS_IN_DAY, 0);
  UDate bob = cal->getTime(status);
  cal->add(thisYear ? UCAL_DAY_OF_YEAR : UCAL_YEAR, 1, status);
  return {(int64)bob, (int64)(cal->getTime(status) - 1)};
}

// TODO: error handling
static int weeksInYear(U_ICU_NAMESPACE::TimeZone *tz, UCalendarDaysOfWeek start,
                       int32 year) {
  UErrorCode status = U_ZERO_ERROR;
  auto cal = std::unique_ptr<U_ICU_NAMESPACE::Calendar>(
      U_ICU_NAMESPACE::Calendar::createInstance(tz->clone(), status));
  cal->setFirstDayOfWeek(start);
  cal->setMinimalDaysInFirstWeek(1);
  cal->clear();
  cal->set(UCAL_YEAR, year + 1);
  cal->set(UCAL_DAY_OF_YEAR, 1);
  cal->add(UCAL_DAY_OF_YEAR, -1, status);
  return cal->get(UCAL_WEEK_OF_YEAR, status);
}

class DayPager : public BView {
public:
  DayPager();
  ~DayPager() override;
  bool HasHeightForWidth() override;
  void GetHeightForWidth(float width, float *min, float *max,
                         float *preferred) override;
  void Draw(BRect updateRect) override;
  void AttachedToWindow() override;

private:
};

DayPager::DayPager()
    :
    BView("Day page",
          B_FULL_UPDATE_ON_RESIZE | B_WILL_DRAW | B_NAVIGABLE_JUMP |
              B_FRAME_EVENTS | B_SUPPORTS_LAYOUT) {
  this->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
  this->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
  this->SetExplicitPreferredSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
}

DayPager::~DayPager() {}

bool DayPager::HasHeightForWidth() { return true; }

// TODO: Make start of week configurable
void DayPager::GetHeightForWidth(float width, float *min, float *max,
                                 float *preferred) {
  if (auto parent = dynamic_cast<DatePager *>(this->Parent())) {
    auto columns = weeksInYear(parent->tz, UCAL_MONDAY, parent->year);
    *min = 21; // 7 * 3
    float unit = width / columns;
    *max = unit * 7;
    *preferred = *max;
  } else {
    return BView::GetHeightForWidth(width, min, max, preferred);
  }
}

void DayPager::AttachedToWindow() {
  this->AdoptParentColors();
  this->SetLowColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
}

void DayPager::Draw(BRect updateRect) {
  if (auto parent = dynamic_cast<DatePager *>(this->Parent())) {
    UErrorCode status = U_ZERO_ERROR;
    auto cal = std::unique_ptr<U_ICU_NAMESPACE::Calendar>(
        U_ICU_NAMESPACE::Calendar::createInstance(parent->tz->clone(), status));
    cal->setFirstDayOfWeek(UCAL_MONDAY);
    cal->setMinimalDaysInFirstWeek(1);
    cal->clear();
    cal->set(UCAL_YEAR, parent->year);
    cal->set(UCAL_DAY_OF_YEAR, 1);
    auto end = std::unique_ptr<U_ICU_NAMESPACE::Calendar>(cal->clone());
    end->set(UCAL_YEAR, parent->year + 1);
    auto bounds = this->Bounds();
    for (; cal->before(*end, status); cal->add(UCAL_DAY_OF_YEAR, 1, status)) {
      // TODO: distinguish read and unread messages
      if (parent->hasMessages[cal->getTime(status)]) {
        auto x = cal->get(UCAL_WEEK_OF_YEAR, status);
        auto y = (cal->get(UCAL_DAY_OF_WEEK, status) + 7 - UCAL_MONDAY) % 7;
        this->SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
        auto rect = BRect(bounds.Width() * x, bounds.Height() * y,
                          bounds.Width() * (x + 1), bounds.Height() * (y + 1));
        rect.InsetBy(1, 1);
        this->FillRect(rect, B_SOLID_HIGH);
      }
    }
  }
}
