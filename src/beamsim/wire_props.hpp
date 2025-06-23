#pragma once

#include <beamsim/message.hpp>
#include <beamsim/time.hpp>
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

    static const WireProps kZero;

    static WireProps add(const WireProps &w1, const WireProps &w2) {
      return WireProps{
          Inv{w1.bitrate_inv.v + w2.bitrate_inv.v},
          w1.delay_ms + w2.delay_ms,
      };
    }

    Time delay(MessageSize message_size) const {
      return std::chrono::milliseconds{delay_ms}
           + std::chrono::seconds{
               static_cast<uint64_t>(message_size * bitrate_inv.v)};
    }

    uint64_t bitrate() const {
      return 1 / bitrate_inv.v;
    }

    Inv bitrate_inv;  // "1 / bitrate"
    uint32_t delay_ms;
  };
  const WireProps WireProps::kZero{WireProps::Inv{0}, 0};
}  // namespace beamsim
