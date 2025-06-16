#pragma once

#include <beamsim/example/roles.hpp>
#include <beamsim/ns3/routing.hpp>
#include <beamsim/random.hpp>

namespace beamsim::ns3_ {
  inline void generate(Random &random,
                       Routing &routing,
                       const example::Roles &roles,
                       bool shuffle) {
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

    MpiIndex mpi_routers = 0;
    auto mpi_subnet = [](MpiIndex i) { return (1 + i) % mpiSize(); };

    PeerIndex backbone_router_count = 5;
    std::vector<PeerIndex> backbone;
    for (PeerIndex i = 0; i < backbone_router_count; ++i) {
      backbone.emplace_back(routing.addRouter(mpi_routers));
      for (PeerIndex j = 0; j < i; ++j) {
        routing.wireRouter(backbone.at(i),
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
        region.emplace_back(routing.addRouter(mpi_routers));
        for (auto parent :
             random.sample(std::span{backbone}, random.random(1, 2))) {
          routing.wireRouter(region[j],
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
      subnet.emplace_back(routing.addRouter(mpi_subnet(i)));
      routing.wireRouter(subnet.at(i),
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
      auto subnet_index = shuffled.at(i);
      peers.emplace_back(routing.addPeerNode(mpi_subnet(subnet_index)));
      routing.wirePeer(peers.at(i),
                       subnet.at(subnet_index),
                       {
                           random.random(1, 100) < 30 ? bitrate_business()
                                                      : bitrate_consumer(),
                           delay_local(),
                       });
    }
  }
}  // namespace beamsim::ns3_
