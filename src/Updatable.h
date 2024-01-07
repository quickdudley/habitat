<<<<<<< Updated upstream
#ifndef UPDATABLE_H
#define UPDATABLE_H

template <class T> class Updatable {
public:
  Updatable(const T &value)
      :
      value(value) {}
  T &peek() { return value; }
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
||||||| Stash base
=======
#ifndef UPDATABLE_H
#define UPDATABLE_H

template <class T> class Updatable {
public:
  Updatable(const T &value)
      :
      value(value) {}
  T &peek() { return value; }
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

>>>>>>> Stashed changes
