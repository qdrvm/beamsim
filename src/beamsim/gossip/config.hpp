#pragma once

#include <beamsim/peer_index.hpp>
#include <beamsim/time.hpp>
#include <functional>

namespace beamsim::gossip {
  struct Config {
    PeerIndex mesh_n = 4;
    PeerIndex non_mesh_n = 4;
    bool idontwant = false;

    std::optional<PeerIndex> wfr_robust;
    std::function<Time(PeerIndex, PeerIndex)> wfr_latency;
    Time wfr_latency_threshold = std::chrono::milliseconds{1};
  };
}  // namespace beamsim::gossip
