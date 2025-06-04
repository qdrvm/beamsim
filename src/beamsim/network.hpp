#pragma once

#include <beamsim/i_simulator.hpp>
#include <beamsim/random.hpp>
#include <unordered_map>

namespace beamsim {
  struct Delay {
    Random &random;
    Time min = std::chrono::milliseconds{10};
    Time delta = std::chrono::milliseconds{10};

    Time delay(PeerIndex, PeerIndex, const MessagePtr &) {
      return random.random(min, min + delta);
    }
  };

  // message waits "delay".
  class DelayNetwork : public INetwork {
   public:
    DelayNetwork(Delay &delay) : delay_{delay} {}

    // INetwork
    void send(PeerIndex from_peer,
              PeerIndex to_peer,
              MessagePtr any_message) override {
      simulator_->runAfter(delay_.delay(from_peer, to_peer, any_message),
                           [this,
                            from_peer,
                            to_peer,
                            any_message{std::move(any_message)}]() mutable {
                             simulator_->_receive(
                                 from_peer, to_peer, std::move(any_message));
                           });
    }

   private:
    Delay &delay_;
  };

  // message waits in send queue, then waits "delay", then waits in receive queue.
  // peer can't immediately send or receive 1000 messages over single wire, it will queue.
  class QueueNetwork : public INetwork {
   public:
    QueueNetwork(Delay &delay) : delay_{delay} {}

    // INetwork
    void send(PeerIndex from_peer,
              PeerIndex to_peer,
              MessagePtr message) override {
      auto send_at = next(next_send_, from_peer, message);
      auto delay = delay_.delay(from_peer, to_peer, message);
      simulator_->runAt(
          send_at + delay, [this, from_peer, to_peer, message]() mutable {
            auto receive_at = next(next_receive_, to_peer, message);
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
    Delay &delay_;

   private:
    using Map = std::unordered_map<PeerIndex, Time>;

    Time queueDelay(const MessagePtr &) const {
      return std::chrono::milliseconds(1);
    }

    Time next(Map &map, PeerIndex peer, const MessagePtr &message) {
      auto &next = map[peer];
      next = std::max(next, simulator_->time()) + queueDelay(message);
      return next;
    }

    Map next_send_, next_receive_;
  };
}  // namespace beamsim
