#pragma once

#include <beamsim/example/roles.hpp>
#include <beamsim/gml.hpp>
#include <beamsim/random.hpp>
#include <beamsim/wire_props.hpp>
#include <deque>

namespace beamsim {
  struct DirectRouterConfig {
    std::pair<uint64_t, uint64_t> bitrate;
    std::pair<Time, Time> delay;

    WireProps make(Random &random) const {
      return WireProps{
          random.random(bitrate.first, bitrate.second),
          static_cast<uint32_t>(ms(random.random(delay.first, delay.second))),
      };
    }
  };

  struct Routers {
    struct PeerWire {
      PeerIndex router_index;
      WireProps wire;
    };
    struct Route {
      PeerIndex next;
      WireProps wire;
    };

    static Routers make(Random &random,
                        const example::Roles &roles,
                        bool shuffle) {
      Routers routers;

      auto bitrate = [&](uint64_t min_mbps, uint64_t max_mpbs) {
        constexpr uint64_t mbps = 1000000;
        return [&, min{min_mbps * mbps}, max{max_mpbs * mbps}] {
          return random.random(min, max);
        };
      };
      auto bitrate_backbone = bitrate(10000, 100000);
      auto bitrate_datacenter = bitrate(1000, 10000);
      auto bitrate_business = bitrate(100, 1000);
      auto bitrate_consumer = bitrate(10, 100);
      auto delay = [&](uint32_t min, uint32_t max) {
        return [&, min, max] { return random.random(min, max); };
      };
      auto delay_local = delay(1, 5);
      auto delay_regional = delay(5, 25);
      auto delay_continental = delay(20, 80);

      PeerIndex backbone_router_count = 5;
      std::vector<PeerIndex> backbone;
      for (PeerIndex i = 0; i < backbone_router_count; ++i) {
        backbone.emplace_back(routers.addRouter());
        for (PeerIndex j = 0; j < i; ++j) {
          routers.wireRouter(backbone.at(i),
                             backbone.at(j),
                             {
                                 bitrate_backbone(),
                                 delay_continental(),
                             });
        }
      }

      PeerIndex subnet_count = roles.groups.size();
      PeerIndex region_router_count = std::max<PeerIndex>(1, subnet_count / 8);
      std::vector<std::vector<PeerIndex>> regions;
      for (PeerIndex i = 0; i < region_router_count; ++i) {
        auto &region = regions.emplace_back();
        PeerIndex count = random.random(1, 3);
        for (PeerIndex j = 0; j < count; ++j) {
          region.emplace_back(routers.addRouter());
          for (auto parent :
               random.sample(std::span{backbone}, random.random(1, 2))) {
            routers.wireRouter(region[j],
                               parent,
                               {
                                   bitrate_datacenter(),
                                   delay_regional(),
                               });
          }
        }
        region.emplace_back();
      }

      std::vector<PeerIndex> subnet;
      for (PeerIndex i = 0; i < subnet_count; ++i) {
        auto &region = regions.at(i % regions.size());
        subnet.emplace_back(routers.addRouter());
        routers.wireRouter(subnet.at(i),
                           random.pick(std::span{region}),
                           {
                               bitrate_business(),
                               i % 2 == 0 ? delay_local() : delay_regional(),
                           });
      }

      std::vector<PeerIndex> shuffled;
      for (PeerIndex i = 0; i < roles.validator_count; ++i) {
        shuffled.emplace_back(roles.group_of_validator.at(i));
      }
      if (shuffle) {
        random.shuffle(shuffled);
      }
      std::vector<PeerIndex> peers;
      for (PeerIndex i = 0; i < roles.validator_count; ++i) {
        routers.addPeer(subnet.at(shuffled.at(i)),
                        {
                            random.random(1, 100) < 30 ? bitrate_business()
                                                       : bitrate_consumer(),
                            delay_local(),
                        });
      }

      return routers;
    }

    void makeDirect(const example::Roles &roles,
                    uint64_t max_bitrate,
                    const auto &f) {
      WireProps peer_wire =
          max_bitrate == 0 ? WireProps::kZero : WireProps{max_bitrate, 0};
      auto n = roles.validator_count;
      peer_wire_.reserve(n);
      router_wires_.resize(n);
      routes_.resize(n);
      for (auto &row : routes_) {
        row.resize(n, {0, WireProps::kZero});
      }
      for (PeerIndex i1 = 0; i1 < n; ++i1) {
        peer_wire_.emplace_back(i1, peer_wire);
        for (PeerIndex i2 = 0; i2 < i1; ++i2) {
          auto wire = f(i1, i2);
          wireRouter(i1, i2, wire);
          routes_.at(i1).at(i2) = {i2, wire};
          routes_.at(i2).at(i1) = {i1, wire};
        }
      }
    }

    static Routers make(Random &random,
                        const example::Roles &roles,
                        const DirectRouterConfig &config,
                        uint64_t max_bitrate) {
      Routers routers;
      routers.makeDirect(roles, max_bitrate, [&](PeerIndex, PeerIndex) {
        return config.make(random);
      });
      return routers;
    }

    static Routers make(Random &random,
                        const example::Roles &roles,
                        const Gml &gml,
                        uint64_t gml_bitrate,
                        uint64_t max_bitrate) {
      Routers routers;
      auto g = gml.nodes.size();
      // TODO: shuffle
      std::vector<uint32_t> city_delay_ms;
      if (roles.validator_count > g) {
        for (PeerIndex i1 = 1; i1 < g; ++i1) {
          for (PeerIndex i2 = 0; i2 < i1; ++i2) {
            if (gml.nodes.at(i1).city == gml.nodes.at(i2).city) {
              city_delay_ms.emplace_back(gml.latency_us.at(i1, i2) / 1000);
            }
          }
        }
      }
      routers.makeDirect(roles, max_bitrate, [&](PeerIndex i1, PeerIndex i2) {
        auto n1 = i1 % g;
        auto n2 = i2 % g;
        auto &node1 = gml.nodes.at(n1);
        auto &node2 = gml.nodes.at(n2);
        uint64_t bitrate = gml_bitrate != 0
                             ? gml_bitrate
                             : std::min(node1.bitrate(), node2.bitrate());
        uint32_t delay_ms = 0;
        if (i1 < g) {
          delay_ms = gml.latency_us.at(i1, i2) / 1000;
        } else if (n2 == n1) {
          delay_ms = random.pick(std::span{city_delay_ms});
        } else {
          delay_ms = routers.routes_.at(n1).at(i2).wire.delay_ms;
        }
        return WireProps{bitrate, delay_ms};
      });
      return routers;
    }

    void addPeer(PeerIndex router, const WireProps &wire) {
      peer_wire_.emplace_back(router, wire);
    }

    PeerIndex addRouter() {
      PeerIndex index = router_wires_.size();
      router_wires_.emplace_back();
      return index;
    }

    void wireRouter(PeerIndex router1,
                    PeerIndex router2,
                    const WireProps &wire) {
      router_wires_.at(router1).emplace(router2, wire);
      router_wires_.at(router2).emplace(router1, wire);
    }

    void computeRoutes() {
      if (not routes_.empty()) {
        return;
      }
      auto less = [](const WireProps &w1, const WireProps &w2) {
        return w1.bitrate_inv.v <= w2.bitrate_inv.v
           and w1.delay_ms <= w2.delay_ms;
      };
      auto n = router_wires_.size();
      routes_.resize(n);
      for (PeerIndex i1 = 0; i1 < n; ++i1) {
        routes_.at(i1).resize(n, Route{i1, WireProps::kZero});
      }
      std::deque<Route> queue;
      for (PeerIndex i0 = 0; i0 < n; ++i0) {
        queue.emplace_back(i0, WireProps::kZero);
        while (not queue.empty()) {
          auto [i1, wire10] = queue.front();
          queue.pop_front();
          for (auto &[i2, wire21] : router_wires_.at(i1)) {
            if (i2 == i0) {
              continue;
            }
            auto &route20 = routes_.at(i2).at(i0);
            auto wire20 = WireProps::add(wire10, wire21);
            if (route20.wire.bitrate_inv.v == 0 or less(wire20, route20.wire)) {
              route20.next = i1;
              route20.wire = wire20;
              queue.emplace_back(i2, wire20);
            }
          }
        }
      }
      for (PeerIndex i1 = 0; i1 < n; ++i1) {
        auto &row = routes_.at(i1);
        for (PeerIndex i2 = 0; i2 < n; ++i2) {
          assert2(i2 == i1 or row.at(i2).next != i1);
        }
      }
    }

    auto directWire3(PeerIndex peer1, PeerIndex peer2) const {
      auto &[router1, wire1] = peer_wire_.at(peer1);
      auto &[router2, wire2] = peer_wire_.at(peer2);
      auto &wire12 = routes_.at(router1).at(router2).wire;
      return std::make_tuple(
          std::cref(wire1), std::cref(wire12), std::cref(wire2));
    }
    WireProps directWire(PeerIndex peer1, PeerIndex peer2) const {
      auto [wire1, wire12, wire2] = directWire3(peer1, peer2);
      return WireProps::add(wire1, WireProps::add(wire12, wire2));
    }

    std::vector<PeerWire> peer_wire_;
    std::vector<std::unordered_map<PeerIndex, WireProps>> router_wires_;
    std::vector<std::vector<Route>> routes_;
  };
}  // namespace beamsim
