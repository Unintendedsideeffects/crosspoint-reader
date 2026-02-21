#pragma once
// Host test stub — SpiBusMutex::Guard is a no-op on the host.
struct SpiBusMutex {
  struct Guard {
    Guard() = default;
    ~Guard() = default;
  };
  static void lock() {}
  static void unlock() {}
};
