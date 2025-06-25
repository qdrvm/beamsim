#pragma once

#include <beamsim/i_simulator.hpp>

namespace beamsim {
  // simulated cpu-bound task delay queue
  class Thread {
   public:
    // get task completion time to use with `ISimulator::runAt`
    Time next(ISimulator &simulator, Time delay) {
      next_ = std::max(next_, simulator.time()) + delay;
      return next_;
    }
    void run(ISimulator &simulator, Time delay, OnTimer &&on_timer) {
      simulator.runAt(next(simulator, delay), std::move(on_timer));
    }

   private:
    Time next_;
  };
}  // namespace beamsim
