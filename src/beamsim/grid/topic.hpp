#pragma once

#include <beamsim/example/roles.hpp>
#include <beamsim/grid/grid.hpp>
#include <beamsim/topic_index.hpp>

namespace beamsim::grid {
  struct Topic {
    PeerIndex indexOf(PeerIndex i) const {
      return index_of_ == nullptr ? i : index_of_->at(i);
    }

    void publishTo(PeerIndex peer_index, auto &&f) const {
      Grid(peers_.size()).publishTo(indexOf(peer_index), [&](PeerIndex i) {
        f(peers_.at(i));
      });
    }

    bool forwardTo(PeerIndex from_peer, PeerIndex peer_index, auto &&f) const {
      return Grid(peers_.size())
          .forwardTo(indexOf(from_peer), indexOf(peer_index), [&](PeerIndex i) {
            f(peers_.at(i));
          });
    }

    const std::vector<PeerIndex> &peers_;
    const example::IndexOfPeerMap *index_of_;
  };
}  // namespace beamsim::grid
