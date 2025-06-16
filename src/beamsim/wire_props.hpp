#pragma once

#include <cstdint>

namespace beamsim {
  struct WireProps {
    struct Inv {
      double v;
    };

    WireProps(Inv bitrate_inv, uint32_t delay_ms)
        : bitrate_inv{bitrate_inv}, delay_ms{delay_ms} {}
    WireProps(uint64_t bitrate, uint32_t delay_ms)
        : WireProps{Inv{1.0 / bitrate}, delay_ms} {}

    static WireProps add(const WireProps &w1, const WireProps &w2) {
      return WireProps{
          Inv{w1.bitrate_inv.v + w2.bitrate_inv.v},
          w1.delay_ms + w2.delay_ms,
      };
    }

    uint64_t bitrate() const {
      return 1 / bitrate_inv.v;
    }

    Inv bitrate_inv;  // "1 / bitrate"
    uint32_t delay_ms;
  };
}  // namespace beamsim
