#ifndef UPDATABLE_H
#define UPDATABLE_H

#include <SupportDefs.h>

template <class T> class Updatable {
public:
  Updatable(const T &value)
      :
      value(value) {}
  const T &peek() const { return value; }
  int64 threshold() const { return weight; }
  void put(const T &value, int64 weight) {
    if (weight > this->weight) {
      this->value = value;
      this->weight = weight;
    }
  }
  template <class F> bool check(F &&producer, int64 weight) {
    if (weight > this->weight) {
      if (producer(this->value)) {
        this->weight = weight;
        return true;
      }
    }
    return false;
  }

private:
  T value;
  int64 weight = -1;
};

#endif // UPDATABLE_H
