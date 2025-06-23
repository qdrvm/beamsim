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
    Random(uint32_t seed) : engine_{seed} {}

    template <typename T>
    T random(T min, T max) {
      return std::uniform_int_distribution{min, max}(engine_);
    }

    Time random(Time min, Time max) {
      return Time{random(min.count(), max.count())};
    }

    template <typename T>
    auto sample(std::span<T> xs, size_t n) {
      std::vector<std::remove_const_t<T>> r;
      r.reserve(std::min(xs.size(), n));
      std::ranges::sample(xs, std::back_inserter(r), n, engine_);
      return r;
    }

    template <typename T>
    auto &pick(std::span<T> xs) {
      return xs[random<size_t>(0, xs.size() - 1)];
    }

    void shuffle(auto &&r) {
      std::ranges::shuffle(std::forward<decltype(r)>(r), engine_);
    }

   private:
    std::default_random_engine engine_;
  };
}  // namespace beamsim
