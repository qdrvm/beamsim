#pragma once

#include <beamsim/message.hpp>
#include <beamsim/peer_index.hpp>
#include <beamsim/time.hpp>
#include <functional>

namespace beamsim {
  using OnTimer = std::function<void()>;

  class ISimulator;

  class INetwork {
   public:
    virtual ~INetwork() = default;
    virtual void send(PeerIndex from_peer,
                      PeerIndex to_peer,
                      MessagePtr message) = 0;

    ISimulator *simulator_ = nullptr;
  };

  class ISimulator {
   public:
    virtual ~ISimulator() = default;
    virtual Time time() const = 0;
    virtual void stop() = 0;
    virtual void runAt(Time time, OnTimer &&on_timer) = 0;
    virtual void runSoon(OnTimer &&on_timer) = 0;
    virtual void runAfter(Time delay, OnTimer &&on_timer) = 0;

    // TODO: split
    virtual void send(PeerIndex from_peer,
                      PeerIndex to_peer,
                      MessagePtr any_message) = 0;
    virtual void _receive(PeerIndex from_peer,
                          PeerIndex to_peer,
                          MessagePtr any_message) = 0;
    virtual void connect(PeerIndex peer1, PeerIndex peer2) = 0;
  };

  class IPeer {
   public:
    IPeer(ISimulator &simulator, PeerIndex index)
        : simulator_{simulator}, peer_index_{index} {}

    virtual ~IPeer() = default;
    virtual void onStart() = 0;
    virtual void onMessage(PeerIndex from_peer, MessagePtr any_message) = 0;

    void send(PeerIndex to_peer, MessagePtr any_message) {
      simulator_.send(peer_index_, to_peer, std::move(any_message));
    }

    ISimulator &simulator_;
    PeerIndex peer_index_;
  };
}  // namespace beamsim
