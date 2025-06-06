#pragma once

#include <beamsim/example/roles.hpp>
#include <beamsim/ns3/simulator.hpp>
#include <beamsim/random.hpp>

namespace beamsim::ns3_ {
  inline void generate(Random &random,
                       Simulator &simulator,
                       const example::Roles &roles) {
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
      backbone.emplace_back(simulator.addRouter());
      for (PeerIndex j = 0; j < i; ++j) {
        simulator.wireRouter(backbone.at(i),
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
        region.emplace_back(simulator.addRouter());
        for (auto parent :
             random.sample(std::span{backbone}, random.random(1, 2))) {
          simulator.wireRouter(region[j],
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
      subnet.emplace_back(simulator.addRouter());
      simulator.wireRouter(subnet.at(i),
                           random.pick(std::span{region}),
                           {
                               bitrate_business(),
                               i % 2 == 0 ? delay_local() : delay_regional(),
                           });
    }

    std::vector<PeerIndex> peers;
    for (PeerIndex i = 0; i < roles.validator_count; ++i) {
      peers.emplace_back(simulator.addPeerNode());
      simulator.wirePeer(peers.at(i),
                         subnet.at(roles.group_of_validator.at(i)),
                         {
                             random.random(1, 100) < 30 ? bitrate_business()
                                                        : bitrate_consumer(),
                             delay_local(),
                         });
    }
  }
}  // namespace beamsim::ns3_
