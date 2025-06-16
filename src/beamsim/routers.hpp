#pragma once

#include <beamsim/example/roles.hpp>
#include <beamsim/random.hpp>
#include <beamsim/wire_props.hpp>

namespace beamsim {
  struct Routers {
    struct PeerWire {
      PeerIndex router_index;
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

    std::vector<PeerWire> peer_wire_;
    std::vector<std::unordered_map<PeerIndex, WireProps>> router_wires_;
  };
}  // namespace beamsim
