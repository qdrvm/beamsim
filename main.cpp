#include <beamsim/example/message.hpp>
#include <beamsim/example/roles.hpp>
#include <beamsim/gossip/generate.hpp>
#include <beamsim/gossip/peer.hpp>
#include <beamsim/grid/grid.hpp>
#include <beamsim/grid/message.hpp>
#include <beamsim/network.hpp>
#include <beamsim/simulator.hpp>
#include <print>

#ifdef ns3_FOUND
#include <beamsim/ns3/generate.hpp>
#include <beamsim/ns3/simulator.hpp>
#endif

#include "cli.hpp"

// TODO: const -> config

namespace beamsim::example {
  std::string report_lines;
  template <typename... A>
  void report(const ISimulator &simulator, const A &...a) {
    std::string line;
    line += "[";
    auto arg = [&, first = true](const auto &a) mutable {
      if (first) {
        first = false;
      } else {
        line += ",";
      }
      using T = std::remove_cvref_t<decltype(a)>;
      if constexpr (std::integral<T>) {
        line += std::to_string(a);
      } else {
        std::string_view s = a;
        line += "\"";
        line += s;
        line += "\"";
      }
    };
    arg("report");
    arg(ms(simulator.time()));
    (arg(a), ...);
    line += "]";
    line += "\n";
    report_lines += line;
  }
  void report_flush() {
    std::print("{}", report_lines);
  }
}  // namespace beamsim::example

namespace beamsim::example {
  constexpr auto kTimeSignature = std::chrono::milliseconds{20};
  constexpr auto kTimeSnark = std::chrono::milliseconds{200};

  constexpr auto kStopOnCreateSnark2 = true;

  struct SharedState {
    const Roles &roles;
    PeerIndex snark2_received = 0;
    bool done = false;

    PeerIndex snark2_threshold() const {
      return roles.validator_count * 2 / 3 + 1;
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
      if (peer_index_ == group_.local_aggregator) {
        aggregating_snark1.emplace();
      }
      if (peer_index_ == shared_state_.roles.global_aggregator) {
        aggregating_snark2.emplace();
      }
      simulator_.runAfter(kTimeSignature, [this] {
        MessageSignature signature{peer_index_};
        onMessageSignature(signature);
        sendSignature(std::move(signature));
      });
    }

    virtual void sendSignature(MessageSignature message) = 0;
    virtual void sendSnark1(MessageSnark1 message) = 0;
    virtual void sendSnark2(MessageSnark2 message) = 0;

    void onMessageSignature(const MessageSignature &message) {
      if (not aggregating_snark1.has_value()) {
        return;
      }
      aggregating_snark1->peer_indices.set(message.peer_index);
      PeerIndex threshold = group_.validators.size();
      if (aggregating_snark1->peer_indices.ones() < threshold) {
        return;
      }
      auto snark1 = std::move(aggregating_snark1.value());
      aggregating_snark1.reset();
      simulator_.runAfter(
          kTimeSnark, [this, snark1{std::move(snark1)}]() mutable {
            report(simulator_, "snark1_sent", snark1.peer_indices.ones());
            onMessageSnark1(snark1);
            sendSnark1(std::move(snark1));
          });
    }
    void onMessageSnark1(const MessageSnark1 &message) {
      if (not aggregating_snark2.has_value()) {
        return;
      }
      aggregating_snark2->peer_indices.set(message.peer_indices);
      report(simulator_,
             "snark1_received",
             aggregating_snark2->peer_indices.ones());
      if (aggregating_snark2->peer_indices.ones()
          < shared_state_.snark2_threshold()) {
        return;
      }
      auto snark2 = std::move(aggregating_snark2.value());
      aggregating_snark2.reset();
      simulator_.runAfter(kTimeSnark,
                          [this, snark2{std::move(snark2)}]() mutable {
                            report(simulator_, "snark2_sent");
                            if (kStopOnCreateSnark2) {
                              shared_state_.done = true;
                              simulator_.stop();
                              return;
                            }
                            onMessageSnark2(snark2);
                            sendSnark2(std::move(snark2));
                          });
    }
    void onMessageSnark2(const MessageSnark2 &) {
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

    SharedState &shared_state_;
    GroupIndex group_index_;
    const Roles::Group &group_;
    bool snark2_received = false;
    std::optional<MessageSnark1> aggregating_snark1;
    std::optional<MessageSnark2> aggregating_snark2;
  };

  class PeerDirect : public PeerBase {
    using PeerBase::PeerBase;

    // IPeer
    void onMessage(PeerIndex, MessagePtr any_message) override {
      auto &message = dynamic_cast<Message &>(*any_message);
      if (auto *signature = std::get_if<MessageSignature>(&message.variant)) {
        onMessageSignature(*signature);
      } else if (auto *snark1 = std::get_if<MessageSnark1>(&message.variant)) {
        onMessageSnark1(*snark1);
      } else {
        auto &snark2 = std::get<MessageSnark2>(message.variant);
        // local aggregator forwards snark2 from global aggregator to group
        if (peer_index_ == group_.local_aggregator) {
          for (auto &peer_index : group_.validators) {
            if (peer_index == peer_index_) {
              continue;
            }
            if (peer_index == shared_state_.roles.global_aggregator) {
              continue;
            }
            send(peer_index, any_message);
          }
        }
        onMessageSnark2(snark2);
      }
    }

    // PeerBase
    void sendSignature(MessageSignature message) override {
      if (peer_index_ == group_.local_aggregator) {
        return;
      }
      send(group_.local_aggregator, std::make_shared<Message>(message));
    }
    void sendSnark1(MessageSnark1 message) override {
      assert2(peer_index_ == group_.local_aggregator);
      send(shared_state_.roles.global_aggregator,
           std::make_shared<Message>(message));
    }
    void sendSnark2(MessageSnark2 message) override {
      assert2(peer_index_ == shared_state_.roles.global_aggregator);
      auto any_message = std::make_shared<Message>(message);
      for (auto &group : shared_state_.roles.groups) {
        send(group.local_aggregator, any_message);
      }
    }
  };

  constexpr gossip::TopicIndex topic_snark1 = 0;
  constexpr gossip::TopicIndex topic_snark2 = 1;
  inline gossip::TopicIndex topicSignature(GroupIndex group_index) {
    return 2 + group_index;
  }

  class PeerGossip : public PeerBase {
   public:
    PeerGossip(ISimulator &simulator,
               PeerIndex index,
               SharedState &shared_state,
               Random &random)
        : PeerBase{simulator, index, shared_state}, gossip_{*this, random} {}

    // IPeer
    void onStart() override {
      PeerBase::onStart();
      gossip_.start();
    }
    void onMessage(PeerIndex from_peer, MessagePtr any_message) override {
      gossip_.onMessage(
          from_peer, any_message, [&](const MessagePtr &any_message) {
            auto &message = dynamic_cast<Message &>(*any_message);
            if (auto *signature =
                    std::get_if<MessageSignature>(&message.variant)) {
              onMessageSignature(*signature);
            } else if (auto *snark1 =
                           std::get_if<MessageSnark1>(&message.variant)) {
              onMessageSnark1(*snark1);
            } else {
              auto &snark2 = std::get<MessageSnark2>(message.variant);
              onMessageSnark2(snark2);
            }
          });
    }

    // PeerBase
    void sendSignature(MessageSignature message) override {
      gossip_.gossip(topicSignature(shared_state_.roles.group_of_validator.at(
                         peer_index_)),
                     std::make_shared<Message>(message));
    }
    void sendSnark1(MessageSnark1 message) override {
      gossip_.gossip(topic_snark1, std::make_shared<Message>(message));
    }
    void sendSnark2(MessageSnark2 message) override {
      gossip_.gossip(topic_snark2, std::make_shared<Message>(message));
    }

    gossip::Peer gossip_;
  };

  class PeerGrid : public PeerBase {
   public:
    using PeerBase::PeerBase;

    // IPeer
    void onMessage(PeerIndex from_peer, MessagePtr any_message) override {
      auto &grid_message = dynamic_cast<grid::Message &>(*any_message);
      auto &message = dynamic_cast<Message &>(*grid_message.message);
      if (auto *signature = std::get_if<MessageSignature>(&message.variant)) {
        forward(group_.validators,
                group_.index_of_validators,
                from_peer,
                grid_message);
        onMessageSignature(*signature);
      } else if (auto *snark1 = std::get_if<MessageSnark1>(&message.variant)) {
        forward(shared_state_.roles.aggregators,
                shared_state_.roles.index_of_aggregators,
                from_peer,
                grid_message);
        onMessageSnark1(*snark1);
      } else {
        auto &snark2 = std::get<MessageSnark2>(message.variant);
        forward(shared_state_.roles.validators,
                shared_state_.roles.validators,
                from_peer,
                grid_message);
        onMessageSnark2(snark2);
      }
    }

    // PeerBase
    void sendSignature(MessageSignature message) override {
      publish(group_.validators,
              group_.index_of_validators,
              std::make_shared<Message>(message));
    }
    void sendSnark1(MessageSnark1 message) override {
      publish(shared_state_.roles.aggregators,
              shared_state_.roles.index_of_aggregators,
              std::make_shared<Message>(message));
    }
    void sendSnark2(MessageSnark2 message) override {
      publish(shared_state_.roles.validators,
              shared_state_.roles.validators,
              std::make_shared<Message>(message));
    }

    void publish(const std::vector<PeerIndex> &peers,
                 auto &index_of,
                 MessagePtr message) {
      auto grid_message = std::make_shared<grid::Message>(message, 2);
      auto peer = index_of.at(peer_index_);
      grid::Grid(peers.size()).publishTo(peer, [&](PeerIndex i) {
        send(peers.at(i), grid_message);
      });
    }
    void forward(const std::vector<PeerIndex> &peers,
                 auto &index_of,
                 PeerIndex from_peer,
                 const grid::Message &message) {
      if (message.ttl <= 1) {
        return;
      }
      auto message2 =
          std::make_shared<grid::Message>(message.message, message.ttl - 1);
      auto peer = index_of.at(peer_index_);
      grid::Grid(peers.size())
          .forwardTo(index_of.at(from_peer), peer, [&](PeerIndex i) {
            send(peers.at(i), message2);
          });
    }
  };
}  // namespace beamsim::example

void run_simulation(const SimulationConfig &config) {
  auto roles = beamsim::example::Roles::make(
      config.group_count * config.validators_per_group + 1, config.group_count);
  beamsim::gossip::Config gossip_config{3, 1};

  beamsim::Random random;
  auto run = [&](auto &simulator) {
    beamsim::Stopwatch t_run;
    beamsim::example::SharedState shared_state{roles};

    beamsim::example::report(simulator,
                             "info",
                             roles.validator_count,
                             shared_state.snark2_threshold());

    switch (config.topology) {
      case SimulationConfig::Topology::DIRECT: {
        simulator.template addPeers<beamsim::example::PeerDirect>(
            roles.validator_count, shared_state);
        break;
      }
      case SimulationConfig::Topology::GOSSIP: {
        simulator.template addPeers<beamsim::example::PeerGossip>(
            roles.validator_count, shared_state, random);

        auto subscribe = [&](beamsim::gossip::TopicIndex topic_index,
                             const std::vector<beamsim::PeerIndex> &peers) {
          auto views = beamsim::gossip::generate(random, gossip_config, peers);
          for (size_t i = 0; i < peers.size(); ++i) {
            auto &peer_index = peers.at(i);
            if (not simulator.isLocalPeer(peer_index)) {
              continue;
            }
            dynamic_cast<beamsim::example::PeerGossip &>(
                simulator.peer(peer_index))
                .gossip_.subscribe(topic_index, std::move(views.at(i)));
          }
        };

        for (beamsim::example::GroupIndex group_index = 0;
             group_index < roles.groups.size();
             ++group_index) {
          subscribe(beamsim::example::topicSignature(group_index),
                    roles.groups.at(group_index).validators);
        }
        subscribe(beamsim::example::topic_snark1, roles.aggregators);
        subscribe(beamsim::example::topic_snark2, roles.validators);
        break;
      }
      case SimulationConfig::Topology::GRID: {
        simulator.template addPeers<beamsim::example::PeerGrid>(
            roles.validator_count, shared_state);
        break;
      }
    }

    simulator.run(std::chrono::minutes{1});
    auto done = beamsim::mpiAny(shared_state.done);

    if (beamsim::mpiIsMain()) {
      std::println("Time: {}ms, Real: {}ms, Status: {}",
                   beamsim::ms(simulator.time()),
                   beamsim::ms(t_run.time()),
                   done ? "SUCCESS" : "FAILURE");
    }
    beamsim::example::report_flush();
  };

  // Run simulation based on backend
  switch (config.backend) {
    case SimulationConfig::Backend::DELAY: {
      if (beamsim::mpiIsMain()) {
        beamsim::Delay delay{random};
        beamsim::DelayNetwork delay_network{delay};
        beamsim::Simulator simulator{delay_network};
        run(simulator);
      }
      break;
    }
    case SimulationConfig::Backend::QUEUE: {
      if (beamsim::mpiIsMain()) {
        beamsim::Delay delay{random};
        beamsim::QueueNetwork queue_network{delay};
        beamsim::Simulator simulator{queue_network};
        run(simulator);
      }
      break;
    }
    case SimulationConfig::Backend::NS3: {
#ifdef ns3_FOUND
      beamsim::ns3_::Simulator simulator;
      beamsim::ns3_::generate(
          random, simulator.routing_, roles, config.shuffle);
      switch (config.topology) {
        case SimulationConfig::Topology::DIRECT: {
          simulator.message_decode_ = beamsim::example::Message::decode;
          break;
        }
        case SimulationConfig::Topology::GOSSIP: {
          beamsim::gossip::message_decode = beamsim::example::Message::decode;
          simulator.message_decode_ = beamsim::gossip::Message::decode;
          break;
        }
        case SimulationConfig::Topology::GRID: {
          beamsim::grid::message_decode = beamsim::example::Message::decode;
          simulator.message_decode_ = beamsim::grid::Message::decode;
          break;
        }
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
  std::println("ns3 not found");
#endif

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
