#pragma once

#include <ns3/internet-module.h>
#include <ns3/ipv4-address-helper.h>
#include <ns3/ipv4-static-routing-helper.h>
#include <ns3/point-to-point-helper.h>

#include <beamsim/ns3/mpi.hpp>
#include <beamsim/routers.hpp>
#include <beamsim/std_hash.hpp>
#include <beamsim/time.hpp>
#include <unordered_set>

namespace beamsim::ns3_ {
  using InterfaceIndex = uint32_t;
  using NetworkIndex = uint32_t;

  inline size_t countRoutingTableRules(
      ns3::Ptr<ns3::Ipv4RoutingProtocol> routing) {
    if (auto static_routing =
            ns3::DynamicCast<ns3::Ipv4StaticRouting>(routing)) {
      return static_routing->GetNRoutes();
    }
    if (auto global_routing =
            ns3::DynamicCast<ns3::Ipv4GlobalRouting>(routing)) {
      return global_routing->GetNRoutes();
    }
    if (auto list_routing = ns3::DynamicCast<ns3::Ipv4ListRouting>(routing)) {
      size_t count = 0;
      for (uint32_t i = 0; i < list_routing->GetNRoutingProtocols(); ++i) {
        int16_t priority;
        count += countRoutingTableRules(
            list_routing->GetRoutingProtocol(i, priority));
      }
      return count;
    }
    abort();
  }
  inline size_t countRoutingTableRules(const ns3::NodeContainer &nodes) {
    size_t count = 0;
    for (PeerIndex i = 0; i < nodes.GetN(); ++i) {
      count += countRoutingTableRules(
          nodes.Get(i)->GetObject<ns3::Ipv4>()->GetRoutingProtocol());
    }
    return count;
  }

  const ns3::Ipv4Mask kIpMask = "255.255.0.0";

  struct IpGenerator {
    IpGenerator(NetworkIndex network_index) {
      network.Set(network.Get() + (network_index << 16));
    }
    ns3::Ipv4Address generate() {
      ++i;
      return ns3::Ipv4Address{network.Get() | i};
    }

    ns3::Ipv4Address network = "10.1.0.0";
    uint32_t i = 0;
  };

  struct PeerRouterIpSubnet {
    PeerRouterIpSubnet(PeerIndex router_index)
        : ip_generator{1 + router_index}, router_ip{ip_generator.generate()} {}

    IpGenerator ip_generator;
    ns3::Ipv4Address router_ip;
  };

  auto getStaticRouting(const ns3::Ptr<ns3::Node> &node) {
    return ns3::Ipv4StaticRoutingHelper{}.GetStaticRouting(
        node->GetObject<ns3::Ipv4>());
  }

  struct Endpoint {
    Endpoint(ns3::Ptr<ns3::NetDevice> device, const ns3::Ipv4Address &ip)
        : ipv4{device->GetNode()->GetObject<ns3::Ipv4>()},
          routing{getStaticRouting(device->GetNode())},
          ip{ip},
          interface{ipv4->AddInterface(device)} {
      ipv4->AddAddress(interface, {ip, kIpMask});
      ipv4->SetUp(interface);
      routing->RemoveRoute(routing->GetNRoutes() - 1);
    }

    ns3::Ptr<ns3::Ipv4> ipv4;
    ns3::Ptr<ns3::Ipv4StaticRouting> routing;
    ns3::Ipv4Address ip;
    InterfaceIndex interface;
  };

  struct RouterInfo {
    ns3::Ipv4Address ip;
    std::unordered_map<PeerIndex, InterfaceIndex> reverse_edges;
  };

  struct Routing {
    auto _wire(ns3::Ptr<ns3::Node> node1,
               ns3::Ptr<ns3::Node> node2,
               const WireProps &wire) {
      ns3::PointToPointHelper helper;
      helper.SetDeviceAttribute("DataRate", ns3::DataRateValue{wire.bitrate()});
      helper.SetChannelAttribute(
          "Delay", ns3::TimeValue{ns3::MilliSeconds(wire.delay_ms)});
      return helper.Install(node1, node2);
    }

    PeerIndex _addNode(ns3::NodeContainer &nodes, MpiIndex mpi_index) {
      PeerIndex index = nodes.GetN();
      nodes.Create(1, mpi_index);
      internet_stack_.Install(nodes.Get(index));
      if (static_routing_) {
        auto routing = ns3::Create<ns3::Ipv4StaticRouting>();
        nodes.Get(index)->GetObject<ns3::Ipv4>()->SetRoutingProtocol(routing);
        // remove loopback added by "SetRoutingProtocol"
        routing->RemoveRoute(0);
      }
      return index;
    }

    PeerIndex addPeerNode(MpiIndex mpi_index) {
      return _addNode(peers_, mpi_index);
    }

    PeerIndex addRouter(MpiIndex mpi_index) {
      return _addNode(routers_, mpi_index);
    }

    auto _wireGlobal(ns3::Ptr<ns3::Node> node1,
                     ns3::Ptr<ns3::Node> node2,
                     const WireProps &wire) {
      auto interfaces = global_routing_.Assign(_wire(node1, node2, wire));
      global_routing_.NewNetwork();
      return interfaces.GetAddress(0);
    }

    std::pair<Endpoint, Endpoint> _wireStatic(ns3::Ptr<ns3::Node> node1,
                                              const ns3::Ipv4Address &ip1,
                                              ns3::Ptr<ns3::Node> node2,
                                              const ns3::Ipv4Address &ip2,
                                              const WireProps &wire) {
      auto devices = _wire(node1, node2, wire);
      return {{devices.Get(0), ip1}, {devices.Get(1), ip2}};
    }

    void wirePeer(PeerIndex peer, PeerIndex router, const WireProps &wire) {
      assert2(not peer_ips_.contains(peer));
      ns3::Ipv4Address peer_ip;
      auto peer_node = peers_.Get(peer);
      auto router_node = routers_.Get(router);
      if (static_routing_) {
        auto &subnet = peerRouterIpSubnet(router);
        peer_ip = subnet.ip_generator.generate();
        auto [peer_endpoint, router_endpoint] = _wireStatic(
            peer_node, peer_ip, router_node, subnet.router_ip, wire);
        peer_endpoint.routing->SetDefaultRoute(subnet.router_ip,
                                               peer_endpoint.interface);
        router_endpoint.routing->AddHostRouteTo(peer_ip,
                                                router_endpoint.interface);
      } else {
        peer_ip = _wireGlobal(peer_node, router_node, wire);
      }
      peer_ips_.emplace(peer, peer_ip);
      ip_peer_index_.emplace(peer_ip, peer);
    }

    void wireRouter(PeerIndex router1,
                    PeerIndex router2,
                    const WireProps &wire) {
      auto node1 = routers_.Get(router1);
      auto node2 = routers_.Get(router2);
      if (static_routing_) {
        auto &info1 = routerInfo(router1);
        auto &info2 = routerInfo(router2);
        auto [endpoint1, endpoint2] =
            _wireStatic(node1, info1.ip, node2, info2.ip, wire);
        info1.reverse_edges.emplace(router2, endpoint2.interface);
        info2.reverse_edges.emplace(router1, endpoint1.interface);
      } else {
        _wireGlobal(node1, node2, wire);
      }
    }

    void initRouters(const Routers &routers) {
      std::unordered_map<PeerIndex, MpiIndex> mpi_subnets;
      for (auto &wire : routers.peer_wire_) {
        auto it = mpi_subnets.find(wire.router_index);
        if (it == mpi_subnets.end()) {
          mpi_subnets.emplace(wire.router_index,
                              (1 + mpi_subnets.size()) % mpiSize());
        }
      }
      for (PeerIndex i = 0; i < routers.router_wires_.size(); ++i) {
        auto it = mpi_subnets.find(i);
        assert2(addRouter(it == mpi_subnets.end() ? 0 : it->second) == i);
      }
      for (PeerIndex i1 = 0; i1 < routers.router_wires_.size(); ++i1) {
        for (auto [i2, wire] : routers.router_wires_.at(i1)) {
          if (i2 < i1) {
            continue;
          }
          wireRouter(i1, i2, wire);
        }
      }
      for (PeerIndex i1 = 0; i1 < routers.peer_wire_.size(); ++i1) {
        auto &[i2, wire] = routers.peer_wire_.at(i1);
        assert2(addPeerNode(mpi_subnets.at(i2)) == i1);
        wirePeer(i1, i2, wire);
      }
      if (static_routing_) {
        populateStaticRoutingTables(routers);
      }
    }

    void initDirect(const Routers &routers, const auto &mpi_group) {
      for (PeerIndex i = 0; i < routers.peer_wire_.size(); ++i) {
        assert2(addPeerNode(mpi_group(i) % beamsim::mpiSize()) == i);
        auto ip = direct_ip_generator_.generate();
        peer_ips_.emplace(i, ip);
        ip_peer_index_.emplace(ip, i);
      }
      direct_ = &routers;
    }

    void connect(PeerIndex peer1, PeerIndex peer2) {
      assert2(peer1 != peer2);
      if (direct_ == nullptr) {
        return;
      }
      if (not direct_links_
                  .emplace(std::min(peer1, peer2), std::max(peer1, peer2))
                  .second) {
        return;
      }
      auto [endpoint1, endpoint2] =
          _wireStatic(peers_.Get(peer1),
                      peer_ips_.at(peer1),
                      peers_.Get(peer2),
                      peer_ips_.at(peer2),
                      direct_->directWire(peer1, peer2));
      endpoint1.routing->AddHostRouteTo(endpoint2.ip, endpoint1.interface);
      endpoint2.routing->AddHostRouteTo(endpoint1.ip, endpoint2.interface);
    }

    void populateRoutingTables() {
      if (direct_ == nullptr) {
        if (not static_routing_) {
          Stopwatch t_PopulateRoutingTables;
          ns3::Ipv4GlobalRoutingHelper::PopulateRoutingTables();
          if (mpiIsMain()) {
            std::println(
                "PopulateRoutingTables for {} peers and {} routers took {}ms",
                peers_.GetN(),
                routers_.GetN(),
                ms(t_PopulateRoutingTables.time()));
          }
        }
        if (mpiIsMain()) {
          std::println("routing table rules: {}",
                       ns3_::countRoutingTableRules(routers_));
        }
      }
    }

    void populateStaticRoutingTables(const Routers &routers) {
      for (auto &[i0, subnet] : peer_router_ip_subnet_) {
        for (PeerIndex i1 = 0; i1 < routers_.GetN(); ++i1) {
          if (i1 == i0) {
            continue;
          }
          auto i2 = routers.routes_.at(i1).at(i0).next;
          auto interface = router_info_.at(i2).reverse_edges.at(i1);
          getStaticRouting(routers_.Get(i1))
              ->AddNetworkRouteTo(
                  subnet.ip_generator.network, kIpMask, interface);
        }
      }
    }

    PeerRouterIpSubnet &peerRouterIpSubnet(PeerIndex router_index) {
      auto it = peer_router_ip_subnet_.find(router_index);
      if (it == peer_router_ip_subnet_.end()) {
        it = peer_router_ip_subnet_
                 .emplace(router_index, PeerRouterIpSubnet{router_index})
                 .first;
      }
      return it->second;
    }

    RouterInfo &routerInfo(PeerIndex router_index) {
      auto it = router_info_.find(router_index);
      if (it == router_info_.end()) {
        it = router_info_.emplace(router_index, RouterInfo{}).first;
        it->second.ip = router_ip_generator_.generate();
      }
      return it->second;
    }

    bool static_routing_ = true;

    ns3::InternetStackHelper internet_stack_;
    ns3::Ipv4AddressHelper global_routing_{"10.1.1.0", "255.255.255.0"};
    IpGenerator router_ip_generator_{0};
    std::unordered_map<PeerIndex, RouterInfo> router_info_;
    std::unordered_map<PeerIndex, PeerRouterIpSubnet> peer_router_ip_subnet_;
    const Routers *direct_ = nullptr;
    IpGenerator direct_ip_generator_{0};
    std::unordered_set<std::pair<PeerIndex, PeerIndex>, PairHash> direct_links_;
    ns3::NodeContainer peers_;
    ns3::NodeContainer routers_;
    std::unordered_map<PeerIndex, ns3::Ipv4Address> peer_ips_;
    std::unordered_map<ns3::Ipv4Address, PeerIndex, ns3::Ipv4AddressHash>
        ip_peer_index_;
  };
}  // namespace beamsim::ns3_
