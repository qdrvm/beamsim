#pragma once

#include <beamsim/i_simulator.hpp>
#include <beamsim/routers.hpp>

namespace beamsim {
  // message waits "delay".
  class DelayNetwork : public INetwork {
   public:
    DelayNetwork(const Routers &routers) : routers_{routers} {}

    // INetwork
    void send(PeerIndex from_peer,
              PeerIndex to_peer,
              MessagePtr message) override {
      auto message_size = message->dataSize() + message->padding();
      simulator_->runAfter(
          routers_.directWire(from_peer, to_peer).delay(message_size),
          [this, from_peer, to_peer, message{std::move(message)}]() mutable {
            simulator_->_receive(from_peer, to_peer, std::move(message));
          });
    }

   private:
    const Routers &routers_;
  };

  // message waits in send queue, then waits "delay", then waits in receive queue.
  // peer can't immediately send or receive 1000 messages over single wire, it will queue.
  class QueueNetwork : public INetwork {
   public:
    QueueNetwork(const Routers &routers) : routers_{routers} {}

    // INetwork
    void send(PeerIndex from_peer,
              PeerIndex to_peer,
              MessagePtr message) override {
      auto message_size = message->dataSize() + message->padding();
      auto [wire1, wire12, wire2] = routers_.directWire3(from_peer, to_peer);
      auto delay1 = wire1.delay(message_size);
      auto delay12 = wire12.delay(message_size);
      auto delay2 = wire2.delay(message_size);
      auto send_at = next(next_send_, from_peer, delay1);
      simulator_->runAt(
          send_at + delay12,
          [this, from_peer, to_peer, message, delay2]() mutable {
            auto receive_at = next(next_receive_, to_peer, delay2);
            simulator_->runAt(receive_at,
                              [this,
                               from_peer,
                               to_peer,
                               message{std::move(message)}]() mutable {
                                simulator_->_receive(
                                    from_peer, to_peer, std::move(message));
                              });
          });
    }

   private:
    const Routers &routers_;

   private:
    using Map = std::unordered_map<PeerIndex, Time>;

    Time next(Map &map, PeerIndex peer, Time delay) {
      auto &next = map[peer];
      next = std::max(next, simulator_->time()) + delay;
      return next;
    }

    Map next_send_, next_receive_;
  };
}  // namespace beamsim
