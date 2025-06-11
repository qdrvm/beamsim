#include <beamsim/example/message.hpp>
#include <beamsim/example/roles.hpp>
#include <beamsim/gossip/generate.hpp>
#include <beamsim/gossip/peer.hpp>
#include <beamsim/network.hpp>
#include <beamsim/simulator.hpp>
#include <print>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

#ifdef ns3_FOUND
#include <beamsim/ns3/generate.hpp>
#include <beamsim/ns3/simulator.hpp>
#endif

// TODO: const -> config

namespace beamsim::example {
  constexpr auto kTimeSignature = std::chrono::milliseconds{20};
  constexpr auto kTimeSnark = std::chrono::milliseconds{200};

  constexpr auto kStopOnCreateSnark2 = true;

  struct SharedState {
    const Roles &roles;
    PeerIndex snark2_received = 0;
    bool done = false;
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
      simulator_.runAfter(kTimeSnark,
                          [this, snark1{std::move(snark1)}]() mutable {
                            onMessageSnark1(snark1);
                            sendSnark1(std::move(snark1));
                          });
    }
    void onMessageSnark1(const MessageSnark1 &message) {
      if (not aggregating_snark2.has_value()) {
        return;
      }
      aggregating_snark2->peer_indices.set(message.peer_indices);
      PeerIndex threshold = shared_state_.roles.validator_count;
      if (aggregating_snark2->peer_indices.ones() < threshold) {
        return;
      }
      auto snark2 = std::move(aggregating_snark2.value());
      aggregating_snark2.reset();
      simulator_.runAfter(kTimeSnark,
                          [this, snark2{std::move(snark2)}]() mutable {
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
    using PeerBase::PeerBase;

    // IPeer
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

    gossip::Peer gossip_{*this};
  };
}  // namespace beamsim::example

// CLI Configuration
struct SimulationConfig {
  enum class Backend { DELAY, QUEUE, NS3 };
  enum class Topology { DIRECT, GOSSIP };
  
  Backend backend = Backend::DELAY;
  Topology topology = Topology::DIRECT;
  beamsim::example::GroupIndex group_count = 4;
  beamsim::PeerIndex validators_per_group = 3;
  bool help = false;
  
  void print_usage(const char* program_name) {
    std::println("Usage: {} [options]", program_name);
    std::println("Options:");
    std::println("  -b, --backend <delay|queue|ns3>  Simulation backend (default: delay)");
    std::println("  -t, --topology <direct|gossip>   Communication topology (default: direct)");
    std::println("  -g, --groups <number>             Number of validator groups (default: 4)");
    std::println("  -v, --validators <number>         Validators per group (default: 3)");
    std::println("  -h, --help                        Show this help message");
  }
  
  bool parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      
      if (arg == "-h" || arg == "--help") {
        help = true;
        return true;
      } else if (arg == "-b" || arg == "--backend") {
        if (i + 1 >= argc) {
          std::println("Error: --backend requires an argument");
          return false;
        }
        std::string backend_str = argv[++i];
        if (backend_str == "delay") {
          backend = Backend::DELAY;
        } else if (backend_str == "queue") {
          backend = Backend::QUEUE;
        } else if (backend_str == "ns3") {
          backend = Backend::NS3;
        } else {
          std::println("Error: Invalid backend '{}'. Use: delay, queue, ns3", backend_str);
          return false;
        }
      } else if (arg == "-t" || arg == "--topology") {
        if (i + 1 >= argc) {
          std::println("Error: --topology requires an argument");
          return false;
        }
        std::string topology_str = argv[++i];
        if (topology_str == "direct") {
          topology = Topology::DIRECT;
        } else if (topology_str == "gossip") {
          topology = Topology::GOSSIP;
        } else {
          std::println("Error: Invalid topology '{}'. Use: direct, gossip", topology_str);
          return false;
        }
      } else if (arg == "-g" || arg == "--groups") {
        if (i + 1 >= argc) {
          std::println("Error: --groups requires an argument");
          return false;
        }
        try {
          group_count = std::stoi(argv[++i]);
          if (group_count <= 0) {
            std::println("Error: Number of groups must be positive");
            return false;
          }
        } catch (const std::exception&) {
          std::println("Error: Invalid number for groups: {}", argv[i]);
          return false;
        }
      } else if (arg == "-v" || arg == "--validators") {
        if (i + 1 >= argc) {
          std::println("Error: --validators requires an argument");
          return false;
        }
        try {
          validators_per_group = std::stoi(argv[++i]);
          if (validators_per_group <= 0) {
            std::println("Error: Number of validators per group must be positive");
            return false;
          }
        } catch (const std::exception&) {
          std::println("Error: Invalid number for validators: {}", argv[i]);
          return false;
        }
      } else {
        std::println("Error: Unknown argument '{}'", arg);
        return false;
      }
    }
    return true;
  }
  
  void validate() {
#ifndef ns3_FOUND
    if (backend == Backend::NS3) {
      std::println("Warning: ns3 backend requested but ns3 not found, falling back to delay");
      backend = Backend::DELAY;
    }
#endif
  }
  
  void print_config() {
    if (!beamsim::mpiIsMain()) return;
    
    std::string backend_str = (backend == Backend::DELAY) ? "delay" :
                             (backend == Backend::QUEUE) ? "queue" : "ns3";
    std::string topology_str = (topology == Topology::DIRECT) ? "direct" : "gossip";
    
    std::println("Configuration:");
    std::println("  Backend: {}", backend_str);
    std::println("  Topology: {}", topology_str);
    std::println("  Groups: {}", group_count);
    std::println("  Validators per group: {}", validators_per_group);
    std::println("  Total validators: {}", group_count * validators_per_group);
    std::println();
  }
};

void run_simulation(const SimulationConfig& config) {
  auto roles = beamsim::example::Roles::make(
      config.group_count * config.validators_per_group + 1, config.group_count);
  beamsim::gossip::Config gossip_config{3, 1};
  
  bool use_gossip = (config.topology == SimulationConfig::Topology::GOSSIP);
  
  auto run = [&](beamsim::Random &random, auto &simulator) {
    beamsim::Stopwatch t_run;
    beamsim::example::SharedState shared_state{roles};
    
    if (use_gossip) {
      simulator.template addPeers<beamsim::example::PeerGossip>(
          roles.validator_count, shared_state);
      
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
    } else {
      simulator.template addPeers<beamsim::example::PeerDirect>(
          roles.validator_count, shared_state);
    }
    
    simulator.run(std::chrono::minutes{1});
    auto done = beamsim::mpiAny(shared_state.done);
    
    if (beamsim::mpiIsMain()) {
      std::println("Time: {}ms, Real: {}ms, Status: {}",
                   beamsim::ms(simulator.time()),
                   beamsim::ms(t_run.time()),
                   done ? "SUCCESS" : "FAILURE");
    }
  };
  
  beamsim::Random random;
  
  // Run simulation based on backend
  switch (config.backend) {
    case SimulationConfig::Backend::DELAY: {
      if (beamsim::mpiIsMain()) {
        beamsim::Delay delay{random};
        beamsim::DelayNetwork delay_network{delay};
        beamsim::Simulator simulator{delay_network};
        run(random, simulator);
      }
      break;
    }
    case SimulationConfig::Backend::QUEUE: {
      if (beamsim::mpiIsMain()) {
        beamsim::Delay delay{random};
        beamsim::QueueNetwork queue_network{delay};
        beamsim::Simulator simulator{queue_network};
        run(random, simulator);
      }
      break;
    }
#ifdef ns3_FOUND
    case SimulationConfig::Backend::NS3: {
      beamsim::ns3_::Simulator simulator;
      simulator.routing_.static_routing_ = true;
      beamsim::ns3_::generate(random, simulator.routing_, roles);
      simulator.message_decode_ = beamsim::example::Message::decode;
      if (use_gossip) {
        beamsim::gossip::message_decode = simulator.message_decode_;
        simulator.message_decode_ = beamsim::gossip::Message::decode;
      }
      run(random, simulator);
      break;
    }
#endif
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
    return 1;
  }
  
  if (config.help) {
    config.print_usage(argv[0]);
    return 0;
  }
  
  config.validate();
  config.print_config();
  
  run_simulation(config);

#ifdef ns3_FOUND
  MPI_Finalize();
#endif
  
  return 0;
}
