#pragma once

#include <beamsim/peer_index.hpp>

namespace beamsim::gossip {
  struct Config {
    PeerIndex mesh_n = 4;
    PeerIndex non_mesh_n = 4;
  };
}  // namespace beamsim::gossip
