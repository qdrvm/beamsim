#pragma once

#include <beamsim/peer_index.hpp>
#include <beamsim/random.hpp>
#include <span>
#include <vector>

namespace beamsim::gossip {
  struct View {
    std::vector<PeerIndex> peers;
    PeerIndex mesh_n = 0;

    auto publishTo() const {
      return std::span{peers}.first(std::min<size_t>(peers.size(), mesh_n));
    }

    auto ihaveTo(Random &random) const {
      auto non_mesh =
          std::span{peers}.subspan(std::min<size_t>(peers.size(), mesh_n));
      return random.sample(non_mesh, mesh_n);
    }
  };
  using Views = std::vector<View>;
}  // namespace beamsim::gossip
