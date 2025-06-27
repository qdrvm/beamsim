#pragma once

#include <beamsim/message.hpp>
#include <beamsim/time.hpp>

namespace beamsim {
  struct Consts {
    Time signature_time = std::chrono::milliseconds{20};
    MessageSize signature_size = 3072;
    MessageSize snark_size = 131072;
    double snark1_threshold = 0.9;
    double snark2_threshold = 0.66;
    double aggregation_rate_per_sec = 1000;
    double snark_recursion_aggregation_rate_per_sec = 100;
    Time pq_signature_verification_time = std::chrono::milliseconds{3};
    Time snark_proof_verification_time = std::chrono::milliseconds{10};
  };

  inline Consts &consts() {
    static Consts consts;
    return consts;
  }
}  // namespace beamsim
