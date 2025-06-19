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
#include <beamsim/ns3/simulator.hpp>
#endif

#include "cli.hpp"

namespace beamsim::example {
  std::string report_lines;

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
    enum Role : uint8_t { Validator, LocalAggregator, GlobalAggregator };
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
      size_t global_aggregators = 1;
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
      auto role =
          peer_index == roles_.global_aggregator ? GlobalAggregator
          : peer_index
                  == roles_.groups.at(roles_.group_of_validator.at(peer_index))
                         .local_aggregator
              ? LocalAggregator
              : Validator;
      per_role.at(role).at(in_out).back() += add;
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
      simulator_.runAfter(consts().signature_time, [this] {
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
          consts().snark_time, [this, snark1{std::move(snark1)}]() mutable {
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
      simulator_.runAfter(consts().snark_time,
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
   public:
    using PeerBase::PeerBase;

    static void connect(ISimulator &simulator, const Roles &roles) {
      for (PeerIndex i = 0; i < roles.validator_count; ++i) {
        auto &group = roles.groups.at(roles.group_of_validator.at(i));
        if (i == group.local_aggregator) {
          simulator.connect(i, roles.global_aggregator);
        } else {
          simulator.connect(i, group.local_aggregator);
        }
      }
    }

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

    static void connect(ISimulator &simulator, const Roles &roles) {
      for (PeerIndex i1 = 0; i1 < roles.validator_count; ++i1) {
        auto &group = roles.groups.at(roles.group_of_validator.at(i1));
        auto connect = [&](const std::vector<PeerIndex> &peers,
                           auto &index_of) {
          auto peer = index_of.at(i1);
          grid::Grid(peers.size()).publishTo(peer, [&](PeerIndex i2) {
            simulator.connect(i1, peers.at(i2));
          });
        };
        connect(group.validators, group.index_of_validators);
        if (i1 == group.local_aggregator or i1 == roles.global_aggregator) {
          connect(roles.aggregators, roles.index_of_aggregators);
        }
        connect(roles.validators, roles.validators);
      }
    }

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
  beamsim::Random random;
  auto roles = beamsim::example::Roles::make(
      config.group_count * config.validators_per_group + 1, config.group_count);
  auto routers = beamsim::Routers::make(random, roles, config.shuffle);
  routers.computeRoutes();
  beamsim::example::Metrics metrics{roles};

  auto run = [&](auto &simulator) {
    beamsim::Stopwatch t_run;
    beamsim::example::SharedState shared_state{roles};

    beamsim::example::report(simulator,
                             "info",
                             roles.validator_count,
                             shared_state.snark2_threshold());
    metrics.begin(simulator);

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

        auto subscribe = [&](beamsim::gossip::TopicIndex topic_index,
                             const std::vector<beamsim::PeerIndex> &peers) {
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
        beamsim::example::PeerGrid::connect(simulator, roles);
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
    beamsim::example::report_flush();
  };

  // Run simulation based on backend
  switch (config.backend) {
    case SimulationConfig::Backend::DELAY: {
      if (beamsim::mpiIsMain()) {
        beamsim::DelayNetwork delay_network{routers};
        beamsim::Simulator simulator{delay_network, &metrics};
        run(simulator);
      }
      break;
    }
    case SimulationConfig::Backend::QUEUE: {
      if (beamsim::mpiIsMain()) {
        beamsim::QueueNetwork queue_network{routers};
        beamsim::Simulator simulator{queue_network, &metrics};
        run(simulator);
      }
      break;
    }
    case SimulationConfig::Backend::NS3:
    case SimulationConfig::Backend::NS3_DIRECT: {
#ifdef ns3_FOUND
      beamsim::ns3_::Simulator simulator{&metrics};
      if (config.backend == SimulationConfig::Backend::NS3) {
        simulator.routing_.initRouters(routers);
      } else {
        simulator.routing_.initDirect(routers, [&](beamsim::PeerIndex i) {
          return roles.group_of_validator.at(i);
        });
      }
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
  if (beamsim::mpiIsMain()) {
    std::println("ns3 not found");
  }
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
