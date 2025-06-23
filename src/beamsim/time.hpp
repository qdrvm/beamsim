#pragma once

#include <chrono>

namespace beamsim {
  // NS3 default time scale
  using Time = std::chrono::microseconds;

  inline Time timeSeconds(double seconds) {
    return std::chrono::microseconds{static_cast<int64_t>(1000000 * seconds)};
  }

  auto ms(auto &&time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time).count();
  }

  struct Stopwatch {
    using Clock = std::chrono::steady_clock;

    Stopwatch() : start_{Clock::now()} {}

    auto time() const {
      return Clock::now() - start_;
    }

    Clock::time_point start_;
  };
}  // namespace beamsim
