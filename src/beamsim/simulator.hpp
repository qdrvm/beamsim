#pragma once

#include <beamsim/i_simulator.hpp>
#include <beamsim/set_max.hpp>
#include <map>

namespace beamsim {
  class Simulator : public ISimulator {
   public:
    Simulator(INetwork &network, IMetrics *metrics)
        : network_{network}, metrics_{metrics} {
      network_.simulator_ = this;
    }

    // ISimulator
    Time time() const override {
      return time_;
    }
    void stop() override {
      stop_ = true;
    }
    void runAt(Time time, OnTimer &&on_timer) override {
      if (stop_) {
        return;
      }
      timers_.emplace(time, std::move(on_timer));
      setMax(metric_max_timers_, timers_.size());
    }
    void runSoon(OnTimer &&on_timer) override {
      runAt(time(), std::move(on_timer));
    }
    void runAfter(Time delay, OnTimer &&on_timer) override {
      runAt(time() + delay, std::move(on_timer));
    }
    void send(PeerIndex from_peer,
              PeerIndex to_peer,
              MessagePtr any_message) override {
      if (metrics_ != nullptr) {
        metrics_->onPeerSentMessage(from_peer);
        metrics_->onPeerSentBytes(
            from_peer, any_message->dataSize() + any_message->padding());
      }
      network_.send(from_peer, to_peer, std::move(any_message));
    }
    // used by "INetwork"
    void _receive(PeerIndex from_peer,
                  PeerIndex to_peer,
                  MessagePtr any_message) override {
      if (metrics_ != nullptr) {
        metrics_->onPeerReceivedMessage(to_peer);
        metrics_->onPeerReceivedBytes(
            to_peer, any_message->dataSize() + any_message->padding());
      }
      peers_.at(to_peer)->onMessage(from_peer, std::move(any_message));
    }
    void connect(PeerIndex, PeerIndex) override {}

    void run(Time timeout) {
      while (not stop_ and not timers_.empty()) {
        auto timer = timers_.extract(timers_.begin());
        if (timer.key() > timeout) {
          break;
        }
        setMax(time_, timer.key());
        timer.mapped()();
      }
    }

    template <typename Peer, typename... A>
    void addPeer(A &&...a) {
      PeerIndex index = peers_.size();
      auto &peer = *peers_.emplace_back(
          std::make_unique<Peer>(*this, index, std::forward<A>(a)...));
      runSoon([&peer] { peer.onStart(); });
    }
    template <typename Peer, typename... A>
    void addPeers(PeerIndex count, A &&...a) {
      for (PeerIndex i = 0; i < count; ++i) {
        addPeer<Peer>(std::forward<A>(a)...);
      }
    }

    auto &peer(PeerIndex peer_index) const {
      return *peers_.at(peer_index);
    }

    bool isLocalPeer(PeerIndex) const {
      return true;
    }

   private:
    INetwork &network_;
    IMetrics *metrics_;
    bool stop_ = false;
    Time time_ = Time::zero();
    // https://en.cppreference.com/w/cpp/container/multimap.html
    //   The order of the key-value pairs whose keys compare equivalent is the order of insertion and does not change.
    std::multimap<Time, OnTimer> timers_;
    size_t metric_max_timers_ = 0;

    std::vector<std::unique_ptr<IPeer>> peers_;
  };
}  // namespace beamsim
