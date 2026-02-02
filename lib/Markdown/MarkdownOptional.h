#pragma once

template <typename T>
class MdOptional {
 public:
  MdOptional() : hasValue(false), value_() {}
  explicit MdOptional(const T& value) : hasValue(true), value_(value) {}

  MdOptional& operator=(const T& value) {
    hasValue = true;
    value_ = value;
    return *this;
  }

  bool has_value() const { return hasValue; }
  const T& value() const { return value_; }
  T& value() { return value_; }
  explicit operator bool() const { return hasValue; }

 private:
  bool hasValue;
  T value_;
};
