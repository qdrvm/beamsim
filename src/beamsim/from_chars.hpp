#pragma once

#include <charconv>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace beamsim {
  template <typename T>
  std::optional<std::pair<T, std::string_view>> numFromChars(
      std::string_view s) {
    T num = T();
    std::string_view suffix = s;
    if constexpr (std::is_floating_point_v<T>) {
      // std::from_chars<double> not supported
      // https://stackoverflow.com/a/78471596
      char *end = const_cast<char *>(s.data()) + 1;
      if constexpr (std::is_same_v<T, double>) {
        num = std::strtod(s.data(), &end);
      } else {
        static_assert(false);
      }
      auto n = end - s.data();
      if (n == 0) {
        return std::nullopt;
      }
      suffix.remove_prefix(n);
    } else {
      auto r = std::from_chars(s.data(), s.data() + s.size(), num);
      if (r.ec != std::errc{}) {
        return std::nullopt;
      }
      suffix.remove_prefix(r.ptr - s.data());
    }
    return std::make_pair(num, suffix);
  }
}  // namespace beamsim
