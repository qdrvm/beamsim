#include <beamsim/gossip/generate.hpp>
#include <beamsim/gossip/peer.hpp>
#include <beamsim/network.hpp>
#include <beamsim/simulator.hpp>
#include <print>

namespace beamsim::example {
  class Message : public IMessage {
   public:
    // IMessage
    size_t size() const override {
      return 1000;
    }
    void hash(MessageHasher &hasher) const override {
      hasher.hash(peer_index);
    }

    Message(PeerIndex peer_index) : peer_index{peer_index} {}

    PeerIndex peer_index;
  };

  struct State {
    PeerIndex peer_count = 10;
    PeerIndex aggregator = 0;

    PeerIndex threshold() const {
      return peer_count;  // * 2 / 3;
    }
    // multiple messages from gossip in single simulator tick
    bool done = false;
    PeerIndex received = 0;

    void onReceived(ISimulator &simulator) {
      if (done) {
        return;
      }
      ++received;
      std::println("time = {}ms, received {}/{}",
                   simulator.time().count() / 1000,
                   received,
                   threshold());
      if (received >= threshold()) {
        done = true;
        std::println("success");
        simulator.stop();
        return;
      }
    }
  };

  class Peer : public IPeer {
   public:
    Peer(ISimulator &simulator, PeerIndex index, State &state)
        : IPeer{simulator, index}, state_{state} {}

    // IPeer
    void onStart() override {
      if (peer_index_ != state_.aggregator) {
        // std::println("{} > {}", peer_index_, state_.aggregator);
        simulator_.send(peer_index_,
                        state_.aggregator,
                        std::make_shared<Message>(peer_index_));
      } else {
        state_.onReceived(simulator_);
      }
    }
    void onMessage(PeerIndex from_peer, MessagePtr) override {
      // std::println("{} < {}", peer_index_, from_peer);
      if (peer_index_ != state_.aggregator) {
        return;
      }
      state_.onReceived(simulator_);
    }

   private:
    State &state_;
  };

  class PeerGossip : public IPeer {
   public:
    PeerGossip(ISimulator &simulator,
               PeerIndex index,
               gossip::View &&view,
               State &state)
        : IPeer{simulator, index}, state_{state}, gossip_{*this} {
      gossip_.subscribe(0, std::move(view));
    }

    // IPeer
    void onStart() override {
      // std::println("{} > ...", peer_index_, state_.aggregator);
      gossip_.gossip(0, std::make_shared<Message>(peer_index_));
      if (peer_index_ == state_.aggregator) {
        state_.onReceived(simulator_);
      }
    }
    void onMessage(PeerIndex from_peer, MessagePtr any_message) override {
      gossip_.onMessage(
          from_peer, any_message, [this](const MessagePtr &any_message) {
            auto &message = dynamic_cast<Message &>(*any_message);
            // std::println("{} < ... < {}", peer_index_, message.peer_index);
            if (peer_index_ != state_.aggregator) {
              return;
            }
            state_.onReceived(simulator_);
          });
    }

   private:
    State &state_;
    gossip::Peer gossip_;
  };
}  // namespace beamsim::example

int main() {
  for (auto gossip : {false, true}) {
    for (auto queue : {false, true}) {
      std::println("gossip={} queue={}", gossip, queue);
      beamsim::Random random;
      beamsim::Delay delay{random};
      beamsim::DelayNetwork delay_network{delay};
      beamsim::QueueNetwork queue_network{delay};
      beamsim::Simulator simulator{queue ? (beamsim::INetwork &)queue_network
                                         : (beamsim::INetwork &)delay_network};
      beamsim::example::State state{.peer_count = 10};
      beamsim::gossip::Config gossip_config{3, 1};
      if (gossip) {
        auto views =
            beamsim::gossip::generate(random, gossip_config, state.peer_count);
        for (auto &view : views) {
          simulator.addPeer<beamsim::example::PeerGossip>(std::move(view),
                                                          state);
        }
        views.clear();
      } else {
        simulator.addPeers<beamsim::example::Peer>(state.peer_count, state);
      }
      simulator.run(std::chrono::minutes{1});
      std::println("time = {}ms", simulator.time().count() / 1000);
      std::println();
    }
  }
}
