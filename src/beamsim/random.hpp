#pragma once

#include <algorithm>
#include <beamsim/time.hpp>
#include <random>
#include <span>
#include <vector>

namespace beamsim {
  // TODO: MPI compatible random
  class Random {
   public:
    template <typename T>
    T random(T min, T max) {
      return std::uniform_int_distribution{min, max}(engine_);
    }

    Time random(Time min, Time max) {
      return Time{random(min.count(), max.count())};
    }

    template <typename T>
    std::vector<T> sample(std::span<const T> xs, size_t n) {
      std::vector<T> r;
      r.reserve(std::min(xs.size(), n));
      std::ranges::sample(xs, std::back_inserter(r), n, engine_);
      return r;
    }

   private:
    std::default_random_engine engine_;
  };
}  // namespace beamsim
