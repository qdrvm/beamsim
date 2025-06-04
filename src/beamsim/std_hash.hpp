#pragma once

#include <cstddef>
#include <utility>

namespace beamsim {
  template <typename T>
  size_t stdHash(const T &v) {
    return std::hash<T>{}(v);
  }

  struct StdHasher {
    template <typename T, typename... A>
    StdHasher &operator()(const T &v, const A &...a) {
      // boost/container_hash/hash.hpp
      seed = seed + 0x9e3779b9 + stdHash(v);
      // boost/container_hash/detail/hash_mix.hpp
      if constexpr (sizeof(size_t) == sizeof(uint32_t)) {
        const uint32_t m1 = 0x21f0aaad;
        const uint32_t m2 = 0x735a2d97;
        seed ^= seed >> 16;
        seed *= m1;
        seed ^= seed >> 15;
        seed *= m2;
        seed ^= seed >> 15;
      } else {
        const uint64_t m = 0xe9846af9b1a615d;
        seed ^= seed >> 32;
        seed *= m;
        seed ^= seed >> 32;
        seed *= m;
        seed ^= seed >> 28;
      }
      if constexpr (sizeof...(a) > 0) {
        (operator()(a), ...);
      }
      return *this;
    }

    template <typename... A>
    StdHasher(const A &...a) {
      operator()(a...);
    }

    size_t seed = 0;
  };

  struct PairHash {
    template <typename T1, typename T2>
    static size_t operator()(const std::pair<T1, T2> &v) {
      return StdHasher{v.first, v.second}.seed;
    }
  };
}  // namespace beamsim
