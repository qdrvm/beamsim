#include <beamsim/example/message.hpp>
#include <beamsim/example/roles.hpp>
#include <beamsim/gossip/generate.hpp>
#include <beamsim/gossip/peer.hpp>
#include <beamsim/grid/message.hpp>
#include <beamsim/grid/topic.hpp>
#include <beamsim/mmap.hpp>
#include <beamsim/network.hpp>
#include <beamsim/simulator.hpp>
#include <beamsim/thread.hpp>

#ifdef ns3_FOUND
#include <beamsim/ns3/simulator.hpp>
#endif

#include "cli.hpp"

namespace beamsim::example {
  std::string report_lines;
  bool report_enable = false;

  struct Json {
    void write(const std::integral auto &v) {
      json += std::to_string(v);
    }
    void write(std::string_view v) {
      json += "\"";
      json += v;
      json += "\"";
    }
    template <typename T>
    void write(const std::vector<T> &v) {
      json += "[";
      auto first = true;
      for (auto &x : v) {
        if (first) {
          first = false;
        } else {
          json += ",";
        }
        write(x);
      }
      json += "]";
    }
    template <typename T, typename... A>
    void writeTuple(const T &v, const A &...a) {
      json += "[";
      write(v);
      auto f = [&](const auto &a) mutable {
        json += ",";
        write(a);
      };
      (f(a), ...);
      json += "]";
    }
    std::string json;
  };

  template <typename... A>
  void report(const ISimulator &simulator, const A &...a) {
    if (not report_enable) {
      return;
    }
    Json json;
    json.writeTuple("report", ms(simulator.time()), a...);
    report_lines += json.json;
    report_lines += "\n";
  }
  void report_flush() {
    if (mpiIsMain()) {
      std::print("{}", report_lines);
      for (MpiIndex i = 1; i < mpiSize(); ++i) {
        std::print("{}", mpiRecvStr(i));
      }
    } else {
      mpiSendStr(0, report_lines);
    }
  }

  class Metrics : public IMetrics {
   public:
    enum InOut : uint8_t { In, Out };
    using PerInOut = std::array<std::vector<uint64_t>, 2>;
    using PerRole = std::array<PerInOut, 3>;

    Metrics(Roles &roles) : roles_{roles} {}

    // IMetrics
    void onPeerReceivedMessage(PeerIndex peer_index) {
      add(messages_, peer_index, In, 1);
    }
    void onPeerSentMessage(PeerIndex peer_index) {
      add(messages_, peer_index, Out, 1);
    }
    void onPeerReceivedBytes(PeerIndex peer_index, MessageSize size) {
      add(bytes_, peer_index, In, size);
    }
    void onPeerSentBytes(PeerIndex peer_index, MessageSize size) {
      add(bytes_, peer_index, Out, size);
    }

    void begin(ISimulator &simulator) {
      simulator_ = &simulator;
      onTimer();
    }

    static constexpr std::chrono::milliseconds kBucket{1};

    void onTimer() {
      for (auto *per_role : {&messages_, &bytes_}) {
        for (auto &per_in_out : *per_role) {
          for (auto &bucket : per_in_out) {
            bucket.emplace_back(0);
          }
        }
      }
      simulator_->runAfter(kBucket, [this] { onTimer(); });
    }

    void end(Time time) {
      size_t global_aggregators = roles_.global_aggregators.size();
      report(*simulator_,
             "metrics-roles",
             roles_.validator_count - roles_.aggregators.size(),
             roles_.aggregators.size() - global_aggregators,
             global_aggregators);
      auto n = time / kBucket;
      std::array<PerRole *, 2> all{&messages_, &bytes_};
      for (auto i1 = 0; i1 < 2; ++i1) {
        for (auto i2 = 0; i2 < 3; ++i2) {
          for (auto i3 = 0; i3 < 2; ++i3) {
            auto &v = all.at(i1)->at(i2).at(i3);
            v.resize(n);
            report(*simulator_, "metrics", i1, i2, i3, v);
          }
        }
      }
    }

    void add(PerRole &per_role,
             PeerIndex peer_index,
             InOut in_out,
             uint64_t add) {
      per_role.at(std::to_underlying(roles_.roles.at(peer_index)))
          .at(in_out)
          .back() += add;
    }

    Roles &roles_;
    ISimulator *simulator_ = nullptr;
    PerRole messages_;
    PerRole bytes_;
  };
}  // namespace beamsim::example

namespace beamsim::example {
  constexpr auto kStopOnCreateSnark2 = true;

  struct SharedState {
    const Roles &roles;
    gossip::Config gossip_config;
    bool snark1_group_once;
    bool snark1_pull;
    bool snark1_pull_early;
    bool signature_half_direct;
    bool snark1_half_direct;
    bool signature_direct;
    PeerIndex publish_signature_more;
    bool stop_on_create_snark1;
    PeerIndex snark2_received = 0;
    bool done = false;
    std::optional<std::pair<uint32_t, uint32_t>> signature_duplicates;

    PeerIndex snark1_threshold(const Roles::Group &group) const {
      return group.validators.size() * consts().snark1_threshold;
    }
    PeerIndex plot_snark1_threshold() const {
      PeerIndex sum = 0;
      for (auto &group : roles.groups) {
        sum += snark1_threshold(group);
      }
      return sum;
    }

    PeerIndex snark2_threshold() const {
      return roles.validator_count * consts().snark2_threshold;
    }

    PeerIndex directSnark1(PeerIndex from_peer) const {
      return roles.global_aggregators.at(from_peer
                                         % roles.global_aggregators.size());
    }
    void directSignature(PeerIndex from_peer, const auto &f) const {
      auto &group = roles.groups.at(roles.group_of_validator.at(from_peer));
      if (roles.roles.at(from_peer) == Role::LocalAggregator) {
        for (auto &to_peer : group.local_aggregators) {
          if (to_peer == from_peer) {
            continue;
          }
          f(to_peer);
        }
      } else {
        f(group.local_aggregators.at(group.index_of_validators.at(from_peer)
                                     % group.local_aggregators.size()));
      }
    }
    void connectHalfDirect(ISimulator &simulator) const {
      if (signature_half_direct) {
        for (auto &group : roles.groups) {
          for (auto &from_peer : group.validators) {
            if (roles.roles.at(from_peer) == Role::LocalAggregator) {
              continue;
            }
            directSignature(from_peer, [&](PeerIndex to_peer) {
              simulator.connect(from_peer, to_peer);
            });
          }
        }
      }
      if (snark1_half_direct) {
        for (auto &group : roles.groups) {
          for (auto &from_peer : group.local_aggregators) {
            simulator.connect(from_peer, directSnark1(from_peer));
          }
        }
      }
    }
  };

  class PeerBase : public IPeer {
   public:
    PeerBase(ISimulator &simulator, PeerIndex index, SharedState &shared_state)
        : IPeer{simulator, index},
          shared_state_{shared_state},
          group_index_{shared_state_.roles.group_of_validator.at(peer_index_)},
          group_{shared_state_.roles.groups.at(group_index_)} {}

    // IPeer
    void onStart() override {
      if (role() == Role::LocalAggregator) {
        aggregating_snark1.emplace();
      }
      if (role() == Role::GlobalAggregator) {
        aggregating_snark2.emplace();
      }
      thread_.run(simulator_, consts().signature_time, [this] {
        MessageSignature signature{peer_index_};
        _onMessageSignature(signature);
        sendSignature(std::make_shared<Message>(std::move(signature)));
      });
    }

    virtual void sendSignature(MessagePtr message) = 0;
    virtual void sendSnark1(MessagePtr message) = 0;
    virtual void sendSnark2(MessageSnark2 message) = 0;

    bool onMessagePull(PeerIndex from_peer,
                       const MessagePtr &any_message,
                       MessageForwardFn forward) {
      auto *message = dynamic_cast<const Message *>(any_message.get());
      if (not message) {
        return false;
      }
      if (auto *signature = std::get_if<MessageSignature>(&message->variant)) {
        if (shared_state_.signature_half_direct) {
          onMessageSignature(
              *signature, [this, any_message] { sendSignature(any_message); });
          return true;
        }
        if (shared_state_.signature_direct) {
          onMessageSignature(*signature,
                             forwardSignatureDirect(from_peer, any_message));
          return true;
        }
      }
      auto snark1_direct =
          shared_state_.snark1_half_direct
          and shared_state_.roles.roles.at(from_peer) == Role::LocalAggregator;
      if (not shared_state_.snark1_pull and not snark1_direct) {
        return false;
      }
      if (auto *ihave = std::get_if<MessageIhaveSnark1>(&message->variant)) {
        if (pulling_.contains(ihave->peer_indices)) {
          return true;
        }

        // For global aggregators using snark1-pull, don't request snark1 from groups we already have
        if (shared_state_.snark1_group_once and shared_state_.snark1_pull
            and role() == Role::GlobalAggregator) {
          // Determine which group this snark1 ihave is from
          auto source_group = getGroupFromPeerIndices(ihave->peer_indices);

          // If the group has already contributed, ignore this ihave
          if (snark1_received_groups_.get(source_group)) {
            report(simulator_,
                   "snark1_ihave_ignored_duplicate_group",
                   source_group);
            return true;
          }
        }

        auto bits1 = pulling_max_.ones();
        pulling_max_.set(ihave->peer_indices);
        auto bits2 = pulling_max_.ones();
        if (bits2 <= bits1) {
          return true;
        }
        if (snark1_direct) {
          forward = [this, any_message] { sendSnark1(any_message); };
        }
        assert2(forward);
        pulling_.emplace(ihave->peer_indices, forward);
        send(from_peer,
             std::make_shared<Message>(
                 MessageIwantSnark1{std::move(ihave->peer_indices)}));
        return true;
      }
      if (auto *iwant = std::get_if<MessageIwantSnark1>(&message->variant)) {
        if (shared_state_.snark1_pull_early
            and not snark1_cache_.contains(iwant->peer_indices)) {
          will_want_[iwant->peer_indices].emplace_back(from_peer);
          return true;
        }
        assert2(snark1_cache_.contains(iwant->peer_indices));
        send(from_peer,
             std::make_shared<Message>(
                 MessageSnark1{std::move(iwant->peer_indices)}));
        return true;
      }
      if (auto *snark1 = std::get_if<MessageSnark1>(&message->variant)) {
        MessageForwardFn forward;
        if (shared_state_.snark1_pull) {
          forward = std::exchange(pulling_.at(snark1->peer_indices), {});
          assert2(forward);
          snark1_cache_.emplace(snark1->peer_indices);
        } else {
          assert2(snark1_direct);
          forward = [this, any_message] { sendSnark1(any_message); };
        }
        onMessageSnark1(*snark1, std::move(forward));
        return true;
      }
      return false;
    }
    void onMessageSignature(const MessageSignature &message,
                            MessageForwardFn forward) {
      thread_.run(simulator_,
                  consts().pq_signature_verification_time,
                  [this, message, forward] {
                    forward();
                    _onMessageSignature(message);
                  });
    }
    void _onMessageSignature(const MessageSignature &message) {
      if (not aggregating_snark1.has_value()) {
        return;
      }
      aggregating_snark1->peer_indices.set(message.peer_index);
      PeerIndex threshold = shared_state_.snark1_threshold(group_);
      auto received = aggregating_snark1->peer_indices.ones();
      if (received < threshold) {
        return;
      }
      auto snark1 = std::move(aggregating_snark1.value());
      aggregating_snark1.reset();
      if (not shared_state_.signature_duplicates) {
        shared_state_.signature_duplicates.emplace(signature_duplicates,
                                                   signatures_seen.ones());
      }
      if (shared_state_.snark1_pull and shared_state_.snark1_pull_early) {
        sendSnark1(
            std::make_shared<Message>(MessageIhaveSnark1{snark1.peer_indices}));
      }
      thread_.run(simulator_,
                  timeSeconds(received / consts().aggregation_rate_per_sec),
                  [this, snark1{std::move(snark1)}]() mutable {
                    report(simulator_,
                           "snark1_sent",
                           group_index_,
                           snark1.peer_indices.ones());
                    if (shared_state_.stop_on_create_snark1) {
                      shared_state_.done = true;
                      simulator_.stop();
                      return;
                    }
                    _onMessageSnark1(snark1);
                    if (shared_state_.snark1_pull) {
                      if (not shared_state_.snark1_pull_early) {
                        sendSnark1(std::make_shared<Message>(
                            MessageIhaveSnark1{snark1.peer_indices}));
                      }
                      snark1_cache_.emplace(std::move(snark1.peer_indices));
                    } else {
                      sendSnark1(std::make_shared<Message>(std::move(snark1)));
                    }
                  });
    }
    void onMessageSnark1(const MessageSnark1 &message,
                         MessageForwardFn forward) {
      thread_.run(simulator_,
                  consts().snark_proof_verification_time,
                  [this, message, forward] {
                    forward();
                    _onMessageSnark1(message);
                  });
    }
    void _onMessageSnark1(const MessageSnark1 &message) {
      if (shared_state_.snark1_pull and shared_state_.snark1_pull_early) {
        auto want = will_want_.extract(message.peer_indices);
        if (want) {
          auto ihave = std::make_shared<Message>(
              MessageIhaveSnark1{message.peer_indices});
          for (auto &peer_to : want.mapped()) {
            send(peer_to, ihave);
          }
        }
      }

      if (not aggregating_snark2.has_value()) {
        return;
      }

      if (shared_state_.snark1_group_once) {
        // Determine which group this snark1 came from by checking the peer indices
        auto source_group = getGroupFromPeerIndices(message.peer_indices);

        // If the group has already contributed, ignore this snark1
        if (snark1_received_groups_.get(source_group)) {
          report(simulator_, "snark1_ignored_duplicate_group", source_group);
          return;
        }

        // Mark this group as having contributed
        snark1_received_groups_.set(source_group);
      }

      ++snark1_received;
      aggregating_snark2->peer_indices.set(message.peer_indices);
      report(simulator_,
             "snark1_received",
             aggregating_snark2->peer_indices.ones());
      auto received = aggregating_snark2->peer_indices.ones();
      if (received < shared_state_.snark2_threshold()) {
        return;
      }
      auto snark2 = std::move(aggregating_snark2.value());
      aggregating_snark2.reset();
      thread_.run(
          simulator_,
          timeSeconds(snark1_received
                      / consts().snark_recursion_aggregation_rate_per_sec),
          [this, snark2{std::move(snark2)}]() mutable {
            report(simulator_, "snark2_sent");
            if (kStopOnCreateSnark2) {
              shared_state_.done = true;
              simulator_.stop();
              return;
            }
            _onMessageSnark2(snark2);
            sendSnark2(std::move(snark2));
          });
    }
    void onMessageSnark2(const MessageSnark2 &message,
                         MessageForwardFn forward) {
      thread_.run(simulator_,
                  consts().snark_proof_verification_time,
                  [this, message, forward] {
                    forward();
                    _onMessageSnark2(message);
                  });
    }
    void _onMessageSnark2(const MessageSnark2 &) {
      if (snark2_received) {
        return;
      }
      snark2_received = true;
      if (shared_state_.done) {
        return;
      }
      ++shared_state_.snark2_received;
      PeerIndex threshold = shared_state_.roles.validator_count;
      if (shared_state_.snark2_received < threshold) {
        return;
      }
      shared_state_.done = true;
      simulator_.stop();
    }
    bool signatureHalfDirect(const MessagePtr &message) {
      if (not shared_state_.signature_half_direct
          or role() == Role::LocalAggregator) {
        return false;
      }
      shared_state_.directSignature(
          peer_index_, [&](PeerIndex to_peer) { send(to_peer, message); });
      return true;
    }
    bool snark1HalfDirect(const MessagePtr &message) {
      if (not shared_state_.snark1_half_direct
          or role() != Role::LocalAggregator) {
        return false;
      }
      send(shared_state_.directSnark1(peer_index_), message);
      return true;
    }
    bool signatureDirect(const MessagePtr &message) {
      if (not shared_state_.signature_direct) {
        return false;
      }
      shared_state_.directSignature(
          peer_index_, [&](PeerIndex to_peer) { send(to_peer, message); });
      return true;
    }

    Role role() const {
      return shared_state_.roles.roles.at(peer_index_);
    }

    // Helper function to determine which group a BitSet of peer_indices belongs to
    GroupIndex getGroupFromPeerIndices(const BitSet &peer_indices) const {
      auto one = peer_indices.findOne(0);
      assert2(one.has_value());
      return shared_state_.roles.group_of_validator.at(one.value());
    }

    MessageForwardFn forwardSignatureDirect(PeerIndex from_peer,
                                            MessagePtr any_message) {
      return [this, from_peer, any_message] {
        // local aggregator forwards signature from validator to local aggregators
        if (shared_state_.roles.roles.at(from_peer) != Role::LocalAggregator
            and role() == Role::LocalAggregator) {
          for (auto &to_peer : group_.local_aggregators) {
            if (to_peer == peer_index_) {
              continue;
            }
            send(to_peer, any_message);
          }
        }
      };
    }

    void checkSignatureDuplicates(const MessagePtr &any_message) {
      if (not aggregating_snark1.has_value()) {
        return;
      }
      if (auto *message = dynamic_cast<Message *>(any_message.get())) {
        if (auto *signature =
                std::get_if<MessageSignature>(&message->variant)) {
          if (signatures_seen.get(signature->peer_index)) {
            ++signature_duplicates;
          } else {
            signatures_seen.set(signature->peer_index);
            assert2(signatures_seen.get(signature->peer_index));
          }
        }
        return;
      }
      if (auto *message = dynamic_cast<gossip::Message *>(any_message.get())) {
        for (auto &publish : message->publish) {
          checkSignatureDuplicates(publish.message);
        }
        return;
      }
      if (auto *message = dynamic_cast<grid::Message *>(any_message.get())) {
        checkSignatureDuplicates(message->message);
        return;
      }
      abort();
    }

    SharedState &shared_state_;
    GroupIndex group_index_;
    const Roles::Group &group_;
    bool snark2_received = false;
    std::optional<MessageSnark1> aggregating_snark1;
    BitSet signatures_seen;
    uint32_t signature_duplicates = 0;
    // TODO: remove when aggregating multiple times
    size_t snark1_received = 0;
    std::optional<MessageSnark2> aggregating_snark2;
    std::unordered_set<BitSet> snark1_cache_;
    std::unordered_map<BitSet, MessageForwardFn> pulling_;
    std::unordered_map<BitSet, std::vector<PeerIndex>> will_want_;
    BitSet pulling_max_;
    // Track which groups have already contributed snark1 (for global aggregators)
    BitSet snark1_received_groups_;
    Thread thread_;
  };

  class PeerDirect : public PeerBase {
   public:
    using PeerBase::PeerBase;

    static void connect(ISimulator &simulator, const Roles &roles) {
      for (PeerIndex i = 0; i < roles.validator_count; ++i) {
        auto &group = roles.groups.at(roles.group_of_validator.at(i));
        for (auto &to_peer : group.local_aggregators) {
          if (to_peer == i) {
            continue;
          }
          simulator.connect(i, to_peer);
        }
        if (roles.roles.at(i) != Role::Validator) {
          for (auto &to_peer : roles.global_aggregators) {
            if (to_peer == i) {
              continue;
            }
            simulator.connect(i, to_peer);
          }
        }
      }
    }

    // IPeer
    void onMessage(PeerIndex from_peer, MessagePtr any_message) override {
      checkSignatureDuplicates(any_message);
      auto forward_snark1 = [this, from_peer, any_message] {
        // global aggregator forwards snark1 from local aggregator to global aggregators
        if (shared_state_.roles.roles.at(from_peer) == Role::LocalAggregator
            and role() == Role::GlobalAggregator) {
          for (auto &to_peer : shared_state_.roles.global_aggregators) {
            if (to_peer == peer_index_) {
              continue;
            }
            send(to_peer, any_message);
          }
        }
      };
      if (onMessagePull(from_peer, any_message, forward_snark1)) {
        return;
      }
      auto &message = dynamic_cast<Message &>(*any_message);
      if (auto *signature = std::get_if<MessageSignature>(&message.variant)) {
        onMessageSignature(*signature,
                           forwardSignatureDirect(from_peer, any_message));
      } else if (std::holds_alternative<MessageIhaveSnark1>(message.variant)) {
        onMessagePull(from_peer, any_message, std::move(forward_snark1));
      } else if (auto *snark1 = std::get_if<MessageSnark1>(&message.variant)) {
        onMessageSnark1(*snark1, std::move(forward_snark1));
      } else {
        auto &snark2 = std::get<MessageSnark2>(message.variant);
        onMessageSnark2(snark2, [] {
          std::println("not implemented: PeerDirect forward snark2");
          abort();
        });
      }
    }

    // PeerBase
    void sendSignature(MessagePtr message) override {
      shared_state_.directSignature(
          peer_index_, [&](PeerIndex to_peer) { send(to_peer, message); });
    }
    void sendSnark1(MessagePtr message) override {
      if (role() == Role::LocalAggregator) {
        send(shared_state_.directSnark1(peer_index_), message);
      } else {
        assert2(role() == Role::GlobalAggregator);
        for (auto &to_peer : shared_state_.roles.global_aggregators) {
          if (to_peer == peer_index_) {
            continue;
          }
          send(to_peer, message);
        }
      }
    }
    void sendSnark2(MessageSnark2 message) override {
      assert2(role() == Role::GlobalAggregator);
      auto any_message = std::make_shared<Message>(message);
      std::println("not implemented: PeerDirect send snark2");
      abort();
    }
  };

  constexpr TopicIndex topic_snark1 = 0;
  constexpr TopicIndex topic_snark2 = 1;
  inline TopicIndex topicSignature(GroupIndex group_index) {
    return 2 + group_index;
  }

  class PeerGossip : public PeerBase {
   public:
    PeerGossip(ISimulator &simulator,
               PeerIndex index,
               SharedState &shared_state,
               Random &random)
        : PeerBase{simulator, index, shared_state},
          gossip_{*this, random, shared_state_.gossip_config},
          random_{random} {}

    // IPeer
    void onStart() override {
      PeerBase::onStart();
      gossip_.start();
    }
    void onMessage(PeerIndex from_peer, MessagePtr any_message) override {
      checkSignatureDuplicates(any_message);
      if (onMessagePull(from_peer, any_message, nullptr)) {
        return;
      }
      gossip_.onMessage(
          from_peer,
          any_message,
          [&](const MessagePtr &any_message, MessageForwardFn forward) {
            auto &message = dynamic_cast<Message &>(*any_message);
            if (auto *signature =
                    std::get_if<MessageSignature>(&message.variant)) {
              onMessageSignature(*signature, std::move(forward));
            } else if (std::holds_alternative<MessageIhaveSnark1>(
                           message.variant)) {
              onMessagePull(from_peer, any_message, std::move(forward));
            } else if (auto *snark1 =
                           std::get_if<MessageSnark1>(&message.variant)) {
              onMessageSnark1(*snark1, std::move(forward));
            } else {
              auto &snark2 = std::get<MessageSnark2>(message.variant);
              onMessageSnark2(snark2, std::move(forward));
            }
          });
    }

    // PeerBase
    void sendSignature(MessagePtr message) override {
      if (signatureHalfDirect(message) or signatureDirect(message)) {
        return;
      }
      auto topic_index = topicSignature(
          shared_state_.roles.group_of_validator.at(peer_index_));
      if (shared_state_.publish_signature_more
          > shared_state_.gossip_config.mesh_n) {
        auto peers =
            random_.sample(std::span{shared_state_.signature_half_direct
                                         ? group_.local_aggregators
                                         : group_.validators},
                           shared_state_.publish_signature_more);
        peers.erase(std::remove_if(
            peers.begin(), peers.end(), [this](PeerIndex peer_index) {
              return peer_index == peer_index_;
            }));
        gossip_.gossip(topic_index, std::move(message), peers);
      } else {
        gossip_.gossip(topic_index, std::move(message));
      }
    }
    void sendSnark1(MessagePtr message) override {
      if (snark1HalfDirect(message)) {
        return;
      }
      gossip_.gossip(topic_snark1, message);
    }
    void sendSnark2(MessageSnark2 message) override {
      gossip_.gossip(topic_snark2, std::make_shared<Message>(message));
    }

    gossip::Peer gossip_;
    Random &random_;
  };

  class PeerGrid : public PeerBase {
   public:
    using PeerBase::PeerBase;

    // IPeer
    void onMessage(PeerIndex from_peer, MessagePtr any_message) override {
      checkSignatureDuplicates(any_message);
      if (onMessagePull(from_peer, any_message, nullptr)) {
        return;
      }
      auto grid_message = std::dynamic_pointer_cast<grid::Message>(any_message);
      auto forward_snark1 = [this, from_peer, grid_message] {
        forward(topic_snark1, from_peer, grid_message);
      };
      auto &message = dynamic_cast<Message &>(*grid_message->message);
      if (not seen_.emplace(message.hash()).second) {
        return;
      }
      if (auto *signature = std::get_if<MessageSignature>(&message.variant)) {
        onMessageSignature(*signature, [this, from_peer, grid_message] {
          forward(topicSignature(group_index_), from_peer, grid_message);
        });
      } else if (std::holds_alternative<MessageIhaveSnark1>(message.variant)) {
        onMessagePull(
            from_peer, grid_message->message, std::move(forward_snark1));
      } else if (auto *snark1 = std::get_if<MessageSnark1>(&message.variant)) {
        onMessageSnark1(*snark1, std::move(forward_snark1));
      } else {
        auto &snark2 = std::get<MessageSnark2>(message.variant);
        onMessageSnark2(snark2, [this, from_peer, grid_message] {
          forward(topic_snark2, from_peer, grid_message);
        });
      }
    }

    // PeerBase
    void sendSignature(MessagePtr message) override {
      if (signatureHalfDirect(message) or signatureDirect(message)) {
        return;
      }
      publish(topicSignature(group_index_), std::move(message));
    }
    void sendSnark1(MessagePtr message) override {
      if (snark1HalfDirect(message)) {
        return;
      }
      publish(topic_snark1, message);
    }
    void sendSnark2(MessageSnark2 message) override {
      publish(topic_snark2, std::make_shared<Message>(message));
    }

    void publish(TopicIndex topic_index, MessagePtr message) {
      auto grid_message = std::make_shared<grid::Message>(message, 2);
      topics_.at(topic_index).publishTo(peer_index_, [&](PeerIndex to_peer) {
        send(to_peer, grid_message);
      });
    }
    void forward(TopicIndex topic_index,
                 PeerIndex from_peer,
                 std::shared_ptr<grid::Message> message) {
      if (message->ttl <= 1) {
        return;
      }
      auto message2 =
          std::make_shared<grid::Message>(message->message, message->ttl - 1);
      topics_.at(topic_index)
          .forwardTo(from_peer, peer_index_, [&](PeerIndex to_peer) {
            send(to_peer, message2);
          });
    }

    std::unordered_map<TopicIndex, grid::Topic> topics_;
    std::unordered_set<MessageHash> seen_;
  };
}  // namespace beamsim::example

void run_simulation(const SimulationConfig &config) {
  beamsim::Random random{config.random_seed};
  auto roles = beamsim::example::Roles::make(config.roles_config);
  beamsim::Routers routers;
  if (not config.gml_path.empty()) {
    beamsim::Mmap file{config.gml_path};
    if (not file.good()) {
      std::println("can't read gml file: {}", config.gml_path);
      exit(EXIT_FAILURE);
    }
    auto gml = beamsim::Gml::decode(file.data);
    routers = beamsim::Routers::make(
        random, roles, gml, config.gml_bitrate, config.max_bitrate.v);
  } else if (config.direct_router) {
    routers = beamsim::Routers::make(
        random, roles, *config.direct_router, config.max_bitrate.v);
  } else {
    routers = beamsim::Routers::make(random, roles, config.shuffle);
    routers.computeRoutes();
  }
  beamsim::example::Metrics metrics{roles};

  beamsim::example::report_enable = config.report;
  auto *metrics_ptr = config.report ? &metrics : nullptr;

  auto run = [&](auto &simulator) {
    beamsim::Stopwatch t_run;
    beamsim::example::SharedState shared_state{
        .roles = roles,
        .gossip_config = config.gossip_config,
        .snark1_group_once = config.snark1_group_once,
        .snark1_pull = config.snark1_pull,
        .snark1_pull_early = config.snark1_pull_early,
        .signature_half_direct = config.signature_half_direct,
        .snark1_half_direct = config.snark1_half_direct,
        .signature_direct = config.signature_direct,
        .publish_signature_more = config.publish_signature_more,
        .stop_on_create_snark1 = config.local_aggregation_only,
    };

    beamsim::example::report(simulator,
                             "info",
                             roles.validator_count,
                             shared_state.plot_snark1_threshold(),
                             shared_state.snark2_threshold());
    metrics.begin(simulator);

    if (config.topology == SimulationConfig::Topology::DIRECT
        or config.signature_direct) {
      for (size_t i = 0; i < roles.validator_count; ++i) {
        shared_state.directSignature(i, [&](beamsim::PeerIndex to_peer) {
          simulator.connect(i, to_peer);
        });
      }
    }
    shared_state.connectHalfDirect(simulator);
    auto [snark1_group, index_of_snark1_group] =
        shared_state.snark1_half_direct
            ? std::tie(roles.global_aggregators,
                       roles.index_of_global_aggregators)
            : std::tie(roles.aggregators, roles.index_of_aggregators);
    auto subscribe2 = [&](const auto &subscribe) {
      for (beamsim::example::GroupIndex group_index = 0;
           group_index < roles.groups.size();
           ++group_index) {
        auto &group = roles.groups.at(group_index);
        auto [signature_group, index_of_signature_group] =
            shared_state.signature_half_direct
                ? std::tie(group.local_aggregators,
                           group.index_of_local_aggregators)
                : std::tie(group.validators, group.index_of_validators);
        subscribe(beamsim::example::topicSignature(group_index),
                  signature_group,
                  &index_of_signature_group);
      }
      subscribe(
          beamsim::example::topic_snark1, snark1_group, &index_of_snark1_group);
      if (not beamsim::example::kStopOnCreateSnark2) {
        subscribe(beamsim::example::topic_snark2, roles.validators, nullptr);
      }
    };
    switch (config.topology) {
      case SimulationConfig::Topology::DIRECT: {
        simulator.template addPeers<beamsim::example::PeerDirect>(
            roles.validator_count, shared_state);
        beamsim::example::PeerDirect::connect(simulator, roles);
        break;
      }
      case SimulationConfig::Topology::GOSSIP: {
        simulator.template addPeers<beamsim::example::PeerGossip>(
            roles.validator_count, shared_state, random);

        auto subscribe = [&](beamsim::TopicIndex topic_index,
                             const std::vector<beamsim::PeerIndex> &peers,
                             const beamsim::example::IndexOfPeerMap *) {
          auto views =
              beamsim::gossip::generate(random, config.gossip_config, peers);
          for (size_t i = 0; i < peers.size(); ++i) {
            auto &peer_index = peers.at(i);
            for (auto &to_peer : views.at(i).peers) {
              simulator.connect(peer_index, to_peer);
            }
            if (not simulator.isLocalPeer(peer_index)) {
              continue;
            }
            dynamic_cast<beamsim::example::PeerGossip &>(
                simulator.peer(peer_index))
                .gossip_.subscribe(topic_index, std::move(views.at(i)));
          }
        };
        subscribe2(subscribe);
        break;
      }
      case SimulationConfig::Topology::GRID: {
        simulator.template addPeers<beamsim::example::PeerGrid>(
            roles.validator_count, shared_state);

        auto subscribe = [&](beamsim::TopicIndex topic_index,
                             const std::vector<beamsim::PeerIndex> &peers,
                             const beamsim::example::IndexOfPeerMap *index_of) {
          for (size_t i = 0; i < peers.size(); ++i) {
            auto &peer_index = peers.at(i);
            beamsim::grid::Topic topic{peers, index_of};
            topic.publishTo(peer_index, [&](beamsim::PeerIndex to_peer) {
              simulator.connect(peer_index, to_peer);
            });
            if (not simulator.isLocalPeer(peer_index)) {
              continue;
            }
            dynamic_cast<beamsim::example::PeerGrid &>(
                simulator.peer(peer_index))
                .topics_.emplace(topic_index, topic);
          }
        };
        subscribe2(subscribe);
        break;
      }
    }

    simulator.run(std::chrono::minutes{1});
    auto done = beamsim::mpiAny(shared_state.done);
    beamsim::Time simulator_time{beamsim::mpiMin(simulator.time().count())};

    if (beamsim::mpiIsMain()) {
      std::println("Time: {}ms, Real: {}ms, Status: {}",
                   beamsim::ms(simulator_time),
                   beamsim::ms(t_run.time()),
                   done ? "SUCCESS" : "FAILURE");
    }
    metrics.end(simulator_time);
    if (shared_state.signature_duplicates) {
      auto [signature_duplicates, signatures] =
          shared_state.signature_duplicates.value();
      beamsim::example::report(
          simulator, "signature-duplicates", signature_duplicates, signatures);
    }
    beamsim::example::report_flush();
  };

  // Run simulation based on backend
  switch (config.backend) {
    case SimulationConfig::Backend::DELAY: {
      if (beamsim::mpiIsMain()) {
        beamsim::DelayNetwork delay_network{routers};
        beamsim::Simulator simulator{delay_network, metrics_ptr};
        run(simulator);
      }
      break;
    }
    case SimulationConfig::Backend::QUEUE: {
      if (beamsim::mpiIsMain()) {
        beamsim::QueueNetwork queue_network{routers};
        beamsim::Simulator simulator{queue_network, metrics_ptr};
        run(simulator);
      }
      break;
    }
    case SimulationConfig::Backend::NS3:
    case SimulationConfig::Backend::NS3_DIRECT: {
#ifdef ns3_FOUND
      beamsim::ns3_::Simulator simulator{metrics_ptr};
      if (config.backend == SimulationConfig::Backend::NS3) {
        simulator.routing_.initRouters(routers);
      } else {
        simulator.routing_.initDirect(
            routers, config.max_bitrate.v, [&](beamsim::PeerIndex i) {
              return roles.group_of_validator.at(i);
            });
      }
      run(simulator);
#else
      abort();
#endif
      break;
    }
  }
}

int main(int argc, char **argv) {
#ifdef ns3_FOUND
  MPI_Init(&argc, &argv);
#else
  if (beamsim::mpiIsMain()) {
    std::println("ns3 not found");
  }
#endif

  auto &types = beamsim::MessageTypeTable::get();
  types.add<beamsim::example::Message>();
  types.add<beamsim::gossip::Message>();
  types.add<beamsim::grid::Message>();

  SimulationConfig config;

  if (!config.parse_args(argc, argv)) {
    config.print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (config.help) {
    config.print_usage(argv[0]);
    return EXIT_SUCCESS;
  }

  config.validate();
  config.print_config();

  run_simulation(config);

#ifdef ns3_FOUND
  MPI_Finalize();
#endif

  return EXIT_SUCCESS;
}
