#pragma once

#include <beamsim/message.hpp>
#include <beamsim/time.hpp>

namespace beamsim {
  struct Consts {
    Time signature_time = std::chrono::milliseconds{20};
    MessageSize signature_size = 1536;
    MessageSize snark_size = 131072;
    double snark1_threshold = 1;
    double snark2_threshold = 2.0 / 3;
    double aggregation_rate_per_sec = 40;
    double snark_recursion_aggregation_rate_per_sec = 40;
  };

  inline Consts &consts() {
    static Consts consts;
    return consts;
  }
}  // namespace beamsim
