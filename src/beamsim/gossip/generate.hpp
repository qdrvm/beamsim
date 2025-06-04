#pragma once

#include <beamsim/gossip/config.hpp>
#include <beamsim/gossip/view.hpp>
#include <beamsim/std_hash.hpp>
#include <unordered_set>

namespace beamsim::gossip {
  // generate views for peers in "0..peer_count"
  inline Views generate(Random &random,
                        const Config &config,
                        PeerIndex peer_count) {
    auto degree = std::min(config.mesh_n, peer_count - 1);
    Views views;
    views.resize(peer_count);
    std::vector<PeerIndex> free;
    free.reserve(peer_count);
    for (PeerIndex i = 0; i < peer_count; ++i) {
      free.emplace_back(i);
    }
    std::unordered_set<std::pair<size_t, size_t>, PairHash> edges;
    auto edge1 = [&](PeerIndex i1, PeerIndex i2) {
      auto &view = views.at(i1);
      view.peers.emplace_back(i2);
      return view.peers.size() < degree;
    };
    auto edge = [&](PeerIndex fi1, PeerIndex fi2) {
      auto i1 = free.at(fi1), i2 = free.at(fi2);
      if (not edges.emplace(std::min(i1, i2), std::max(i1, i2)).second) {
        return false;
      }
      auto keep1 = edge1(i1, i2), keep2 = edge1(i2, i1);
      // ensure "fi2" is valid after "swap(fi1, last)"
      if (not keep1 and not keep2 and fi2 == free.size() - 1) {
        std::swap(fi1, fi2);
      }
      if (not keep1) {
        std::swap(free.at(fi1), free.back());
        free.pop_back();
      }
      if (not keep2) {
        std::swap(free.at(fi2), free.back());
        free.pop_back();
      }
      return true;
    };
    // sequentially connect all vertices
    for (PeerIndex fi2 = 1; fi2 < peer_count; ++fi2) {
      auto fi1 = fi2 - 1;
      edge(fi1, fi2);
    }
    auto fill = [&] {
      while (free.size() > 1) {
        PeerIndex fi1 = 0;
        PeerIndex offset = 1;
        auto no_edge = true;
        // retry random while there are existing edges
        while (offset < free.size()) {
          auto fi2 = (fi1 + random.random<PeerIndex>(offset, free.size() - 1))
                   % free.size();
          if (edge(fi1, fi2)) {
            no_edge = false;
            break;
          }
          // remove vertex with existing edge from random range
          std::swap(free.at(fi2), free.at((fi1 + offset) % free.size()));
          ++offset;
        }
        // no more possible edges for vertex
        if (no_edge) {
          std::swap(free.at(fi1), free.back());
          free.pop_back();
          break;
        }
      }
    };
    fill();
    for (auto &view : views) {
      view.mesh_n = view.peers.size();
    }
    auto degree2 = std::min(config.mesh_n + config.non_mesh_n, peer_count - 1);
    if (degree < degree2) {
      degree = degree2;
      free.resize(0);
      for (PeerIndex i = 0; i < peer_count; ++i) {
        free.emplace_back(i);
      }
      fill();
    }
    return views;
  }

  // generate views for "peers"
  inline Views generate(Random &random,
                        const Config &config,
                        const std::vector<PeerIndex> &peers) {
    auto views = generate(random, config, peers.size());
    for (auto &view : views) {
      for (auto &i : view.peers) {
        i = peers.at(i);
      }
    }
    return views;
  }
}  // namespace beamsim::gossip
