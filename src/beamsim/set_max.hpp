#pragma once

namespace beamsim {
  // "l = std::max(l, r)"
  template <typename T1, typename T2>
  void setMax(T1 &l, const T2 &r) {
    if (l < r) {
      l = r;
    }
  }
}  // namespace beamsim
