#ifndef NS3_ASSERT_ENABLE
#define NS3_ASSERT_ENABLE
#endif

#include <ns3/applications-module.h>
#include <ns3/internet-module.h>
#include <ns3/point-to-point-helper.h>
#include <ns3/simulator.h>

#include <lsquic-ns3.hpp>
#include <print>

using SocketPtr = ns3::Ptr<ns3::Socket>;

constexpr uint16_t kPort = 10000;
constexpr size_t kServer = 0;
constexpr size_t kClient0 = 1;

const ns3::Ipv4Address kNetwork = "10.1.0.0";
const ns3::Ipv4Mask kNetworkMask = "255.255.0.0";
const uint32_t kNetworkReserved = 2;

ns3::Ipv4Address indexIp(size_t index) {
  return ns3::Ipv4Address{kNetwork.Get()
                          | (uint32_t)(index + kNetworkReserved)};
}
ns3::InetSocketAddress indexAddress(size_t index) {
  return ns3::InetSocketAddress{indexIp(index), kPort};
}
size_t ipIndex(const ns3::Ipv4Address &ip) {
  size_t index = ip.Get() & kNetworkMask.GetInverse();
  NS_ASSERT(index >= kNetworkReserved);
  return index - kNetworkReserved;
}
size_t addressIndex(const ns3::Address &address) {
  return ipIndex(ns3::InetSocketAddress::ConvertFrom(address).GetIpv4());
}

void wireNodes(ns3::PointToPointHelper &wire,
               const ns3::NodeContainer &nodes,
               size_t index1,
               size_t index2) {
  auto devices = wire.Install(nodes.Get(index1), nodes.Get(index2));
  auto route =
      [&](size_t index1, size_t index2, ns3::Ptr<ns3::NetDevice> device) {
        auto ipv4 = device->GetNode()->GetObject<ns3::Ipv4>();
        auto interface = ipv4->AddInterface(device);
        auto routing = ns3::Ipv4StaticRoutingHelper{}.GetStaticRouting(ipv4);
        ipv4->AddAddress(interface, {indexIp(index1), kNetworkMask});
        ipv4->SetUp(interface);
        routing->RemoveRoute(routing->GetNRoutes() - 1);
        routing->AddHostRouteTo(indexIp(index2), interface);
      };
  route(index1, index2, devices.Get(0));
  route(index2, index1, devices.Get(1));
}

struct Config {
  size_t client_count;
  size_t message_size;
};

struct State {
  struct Reading {
    size_t remaining;
    size_t last_time = 0;
  };

  const Config &config;
  bool tcp;
  size_t accept_remaining;
  std::map<size_t, std::map<size_t, Reading>> reading;
  std::map<size_t, std::map<size_t, size_t>> writing;

  State(const Config &config, bool tcp)
      : config{config}, tcp{tcp}, accept_remaining{config.client_count} {
    for (size_t index = kClient0; index <= config.client_count; ++index) {
      reading[kServer].emplace(index, Reading{config.message_size});
      reading[index].emplace(kServer, Reading{config.message_size});
      writing[kServer].emplace(index, 0);
      writing[index].emplace(kServer, 0);
    }
  }
};

struct App : ns3::Application {
  App(size_t index, State &state) : index_{index}, state_{state} {}

  void StartApplication() override {
    if (index_ == kServer) {
      listener_ = makeSocket();
      NS_ASSERT(listener_->Bind(ns3::InetSocketAddress{
                    ns3::Ipv4Address::GetAny(),
                    kPort,
                })
                == 0);
      NS_ASSERT(listener_->Listen() == 0);
      listener_->SetAcceptCallback(
          {}, [this](SocketPtr socket, const ns3::Address &address) {
            --state_.accept_remaining;
            auto index = addressIndex(address);
            sockets_.emplace(index, socket);
            setReadWrite(index);
          });
    }
    if (index_ != kServer) {
      auto index = kServer;
      auto socket = makeSocket();
      sockets_.emplace(index, socket);
      setReadWrite(index);
      NS_ASSERT(socket->Connect(indexAddress(index)) == 0);
      write(index, state_.config.message_size);
    }
  }

  void setReadWrite(size_t index) {
    auto &socket = sockets_.at(index);
    auto &writing = state_.writing.at(index_).at(index);
    socket->SetRecvCallback([this, index](SocketPtr) { pollRead(index); });
    socket->SetSendCallback(
        [this, index, socket, &writing](SocketPtr, uint32_t) {
          pollWrite(index, socket, writing);
        });
  }

  void write(size_t index, size_t size) {
    auto socket = sockets_.at(index);
    auto &writing = state_.writing.at(index_).at(index);
    writing = size;
    pollWrite(index, socket, writing);
  }

  void pollWrite(size_t index, SocketPtr socket, size_t &writing) {
    if (writing == 0) {
      return;
    }
    auto available = socket->GetTxAvailable();
    if (available == 0) {
      return;
    }
    auto packet =
        ns3::Create<ns3::Packet>(std::min<uint32_t>(writing, available));
    auto r = socket->Send(packet, 0);
    if (r == -1) {
      NS_ASSERT(socket->GetErrno() == ns3::Socket::ERROR_AGAIN);
      return;
    }
    NS_ASSERT(r > 0);
    writing -= r;
  }

  void pollRead(size_t index) {
    auto socket = sockets_.at(index);
    while (auto packet = socket->Recv()) {
      auto &reading1 = state_.reading.at(index_);
      auto &reading = reading1.at(index);
      reading.last_time = ns3::Simulator::Now().GetSeconds();
      auto size = packet->GetSize();
      NS_ASSERT(reading.remaining >= size);
      reading.remaining -= size;
      if (reading.remaining == 0) {
        reading1.erase(index);
        if (index_ == kServer) {
          write(index, state_.config.message_size);
        }
        if (reading1.empty()) {
          state_.reading.erase(index_);
        }
        if (state_.reading.empty()) {
          ns3::Simulator::Stop();
        }
      }
    }
  }

  SocketPtr makeSocket() {
    return ns3::Socket::CreateSocket(
        GetNode(),
        ns3::TypeId::LookupByName(state_.tcp ? "ns3::TcpSocketFactory"
                                             : "ns3::LsquicSocketFactory"));
  }

  size_t index_;
  State &state_;
  SocketPtr listener_;
  std::map<size_t, SocketPtr> sockets_;
};

void test(const Config &config, bool tcp) {
  using C = std::chrono::steady_clock;
  auto t0 = C::now();
  std::println("BEGIN clients={} size={} {}",
               config.client_count,
               config.message_size,
               tcp ? "tcp" : "quic");
  State state{config, tcp};
  ns3::PointToPointHelper wire;
  wire.SetDeviceAttribute("DataRate", ns3::DataRateValue{1024 * 1024 * 8});
  wire.SetChannelAttribute("Delay", ns3::TimeValue{ns3::MilliSeconds(1)});
  ns3::NodeContainer nodes;
  nodes.Create(kClient0 + config.client_count);
  for (size_t index = 0; index < nodes.GetN(); ++index) {
    auto node = nodes.Get(index);
    ns3::InternetStackHelper{}.Install(node);
    ns3::InstallLsquic(node);
    node->AddApplication(ns3::Create<App>(index, state));
    if (index != kServer) {
      wireNodes(wire, nodes, index, kServer);
    }
  }

  ns3::Simulator::Stop(ns3::Seconds(60));
  ns3::Simulator::Run();
  if (state.accept_remaining != 0) {
    std::println("accept remaining {}", state.accept_remaining);
  }
  size_t read_remaining_n = 0;
  std::string read_remaining_s;
  for (auto &[index1, reading1] : state.reading) {
    for (auto &[index2, reading] : reading1) {
      read_remaining_n += reading.remaining;
      read_remaining_s += std::format(" {} < {} {} {}s;",
                                      index1,
                                      index2,
                                      reading.remaining,
                                      reading.last_time);
    }
  }
  if (read_remaining_n != 0) {
    std::println("read remaining {}: {}", read_remaining_n, read_remaining_s);
  }
  std::println(
      "END {}ms ({}ms)",
      (size_t)ns3::Simulator::Now().GetMilliSeconds(),
      std::chrono::duration_cast<std::chrono::milliseconds>(C::now() - t0)
          .count());
  ns3::Simulator::Destroy();
  std::println();
}

void test(bool tcp) {
  test({1, 1000000}, tcp);
  test({10, 1 * 1000000}, tcp);
  test({10, 2 * 1000000}, tcp);
  test({10, 4 * 1000000}, tcp);
}

int main() {
  for (auto &tcp : {true, false}) {
    test(tcp);
  }
}
