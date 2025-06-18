#pragma once

#include <beamsim/message.hpp>
#include <beamsim/time.hpp>

namespace beamsim {
  struct Consts {
    Time signature_time = std::chrono::milliseconds{20};
    MessageSize signature_size = 1536;
    Time snark_time = std::chrono::milliseconds{200};
    MessageSize snark_size = 131072;
  };

  inline Consts &consts() {
    static Consts consts;
    return consts;
  }
}  // namespace beamsim
