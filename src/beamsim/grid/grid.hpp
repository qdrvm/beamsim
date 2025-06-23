#pragma once

#include <beamsim/message.hpp>
#include <beamsim/peer_index.hpp>
#include <cmath>

namespace beamsim::grid {
  struct Grid {
    Grid(PeerIndex count)
        : count{count}, width{static_cast<PeerIndex>(std::sqrt(count))} {}

    void publishTo(PeerIndex peer_index, auto &&f) const {
      auto c = _split(peer_index);
      _vertical(peer_index, c.second, f);
      _horizontal(peer_index, c.first, f);
    }

    bool forwardTo(PeerIndex from_peer, PeerIndex peer_index, auto &&f) const {
      [[unlikely]] if (peer_index == from_peer) { return false; }
      auto c = _split(peer_index), o = _split(from_peer);
      if (c.first == o.first) {
        _vertical(peer_index, c.second, f);
        return true;
      }
      if (c.second == o.second) {
        _horizontal(peer_index, c.first, f);
        return true;
      }
      return false;
    }

    std::pair<PeerIndex, PeerIndex> _split(PeerIndex i) const {
      [[unlikely]] if (i > count) { throw std::range_error{"Grid"}; }
      auto x = i % width;
      return {i - x, x};
    }

    void _vertical(PeerIndex center, PeerIndex x, auto &&f) const {
      for (PeerIndex i = x; i < count; i += width) {
        if (i != center) {
          f(i);
        }
      }
    }

    void _horizontal(PeerIndex center, PeerIndex min_x, auto &&f) const {
      auto max_x = std::min(min_x + width, count);
      for (PeerIndex i = min_x; i != max_x; ++i) {
        if (i != center) {
          f(i);
        }
      }
    }

    PeerIndex count;
    PeerIndex width;
  };
}  // namespace beamsim::grid
