#pragma once

#include <ns3/applications-module.h>
#include <ns3/core-module.h>
#include <ns3/internet-module.h>
#include <ns3/network-module.h>
#include <ns3/point-to-point-helper.h>

#include <span>

// "assert" doesn't work
#define assert2(c)                                       \
  if (not(c)) {                                          \
    printf("assert2(%s) %s:%d", #c, __FILE__, __LINE__); \
    abort();                                             \
  }

namespace beam_ns3 {
  using Index = uint32_t;
  using SocketPtr = ns3::Ptr<ns3::Socket>;
  using Bytes = std::vector<uint8_t>;
  template <size_t N>
  using BytesN = std::array<uint8_t, N>;
  using BytesIn = std::span<const uint8_t>;
  using BytesOut = std::span<uint8_t>;
  using Message = Bytes;
  using TimerId = uint32_t;

  struct Buffer {
    Bytes buffer_;
    size_t begin_ = 0, size_ = 0;

    size_t size() const {
      return size_;
    }

    void peek(BytesOut out) const {
      assert2(out.size() <= size());
      auto part = std::min(out.size(), buffer_.size() - begin_);
      memcpy(out.data(), buffer_.data() + begin_, part);
      if (part < out.size()) {
        memcpy(out.data() + part, buffer_.data(), out.size() - part);
      }
    }
    void read(size_t count) {
      assert2(count <= size());
      begin_ = (begin_ + count) % buffer_.size();
      size_ -= count;
    }
    void read(BytesOut out) {
      peek(out);
      read(out.size());
    }

    size_t _end() const {
      return (begin_ + size()) % buffer_.size();
    }
    size_t _writable1() const {
      return std::min(buffer_.size() - size(), buffer_.size() - _end());
    }
    void _align(size_t capacity) {
      Bytes buffer;
      buffer.reserve(capacity);
      buffer.resize(size());
      peek(buffer);
      buffer.resize(capacity);
      std::swap(buffer_, buffer);
      begin_ = 0;
    }
    void write(BytesIn in) {
      if (size() + in.size() > buffer_.size()) {
        _align(size() + in.size());
      }
      auto part = std::min(in.size(), _writable1());
      memcpy(buffer_.data() + _end(), in.data(), part);
      if (part < in.size()) {
        memcpy(buffer_.data(), in.data() + part, in.size() - part);
      }
      size_ += in.size();
    }
    void write(const ns3::Packet &packet) {
      auto n = packet.GetSize();
      if (_writable1() < n) {
        _align(std::max(buffer_.size(), size() + n));
      }
      packet.CopyData(buffer_.data() + _end(), n);
      size_ += n;
    }
  };

  struct SocketState {
    Index index;
    Buffer reading;
    Buffer writing;
  };

  struct SocketInOut {
    SocketPtr in;
    SocketPtr out;
    bool write_out = false;

    auto &write() const {
      return write_out ? out : in;
    }
  };

  const uint16_t kPort = 10000;

  struct WireProps {
    uint64_t bitrate;
    uint32_t delay_ms;
  };

  struct CPy {
    virtual ~CPy() = default;
    virtual void on_start(Index peer) = 0;
    virtual void on_message(Index peer, Index from_peer, Message message) = 0;
    virtual void on_timer(TimerId timer_id) = 0;
  };

  struct encode_u32_be {
    BytesN<4> bytes;
    encode_u32_be() : bytes{} {}
    encode_u32_be(uint32_t n)
        : bytes{(uint8_t)(n >> 24),
                (uint8_t)(n >> 16),
                (uint8_t)(n >> 8),
                (uint8_t)n} {}
    uint32_t value() const {
      return (bytes.at(0) << 24) | (bytes.at(1) << 16) | (bytes.at(2) << 8)
           | bytes.at(3);
    }
  };

  void append(Message &l, auto &&r) {
    l.insert(l.end(), r.begin(), r.end());
  }

  struct Application;
  struct Simulation {
    Simulation() {
      address_helper_.SetBase("10.1.1.0", "255.255.255.0");
    }

    ns3::Ptr<Application> application(Index index) const;

    ns3::InternetStackHelper internet_stack_;
    ns3::Ipv4AddressHelper address_helper_;
    ns3::NodeContainer peers_;
    ns3::NodeContainer routers_;
    ns3::ApplicationContainer applications_;
    std::unordered_map<Index, ns3::Ipv4Address> ips_;
    std::unordered_map<ns3::Ipv4Address, Index, ns3::Ipv4AddressHash> ip_index_;
    CPy *cpy_;
  };
  Simulation simulation;

  struct Application : public ns3::Application {
    static ns3::TypeId GetTypeId() {
      static ns3::TypeId tid = ns3::TypeId("Application")
                                   .SetParent<ns3::Application>()
                                   .AddConstructor<Application>();
      return tid;
    }

    // Application
    void StartApplication() override {
      listen();
      simulation.cpy_->on_start(index_);
    }


    void listen() {
      tcp_listener_ = makeSocket();
      tcp_listener_->Bind(ns3::InetSocketAddress{
          ns3::Ipv4Address::GetAny(),
          kPort,
      });
      tcp_listener_->Listen();
      tcp_listener_->SetAcceptCallback(
          ns3::MakeNullCallback<bool, SocketPtr, const ns3::Address &>(),
          ns3::MakeCallback(&Application::onAccept, this));
    }

    void connect(Index index) {
      auto &sockets = tcp_sockets_[index];
      assert2(not sockets.out);
      if (sockets.write()) {
        return;
      }
      auto socket = makeSocket();
      socket->Connect(ns3::InetSocketAddress{simulation.ips_.at(index), kPort});
      sockets.out = socket;
      sockets.write_out = true;
      add(index, socket);
    }

    void send(Index index, const Message &message) {
      assert2(index != index_);
      auto &sockets = tcp_sockets_[index];
      if (not sockets.write()) {
        connect(index);
      }
      auto &socket = sockets.write();
      auto &state = tcp_socket_state_.at(socket);
      state.writing.write(encode_u32_be(message.size()).bytes);
      state.writing.write(message);
      pollWrite(socket);
    }

    void add(Index index, SocketPtr socket) {
      socket->SetRecvCallback(MakeCallback(&Application::pollRead, this));
      socket->SetSendCallback(MakeCallback(&Application::pollWrite, this));
      tcp_socket_state_.emplace(socket, SocketState{index});
    }

    void onAccept(SocketPtr socket, const ns3::Address &address) {
      auto index = simulation.ip_index_.at(
          ns3::InetSocketAddress::ConvertFrom(address).GetIpv4());
      auto &sockets = tcp_sockets_[index];
      assert2(not sockets.in);
      sockets.in = socket;
      sockets.write_out = (bool)sockets.out;
      add(index, socket);
    }

    void pollRead(SocketPtr socket) {
      auto &state = tcp_socket_state_.at(socket);
      while (auto packet = socket->Recv()) {
        state.reading.write(*packet);
        while (state.reading.size() >= 4) {
          encode_u32_be size;
          state.reading.peek(size.bytes);
          if (state.reading.size() < size.bytes.size() + size.value()) {
            break;
          }
          state.reading.read(size.bytes.size());
          Message message;
          message.resize(size.value());
          state.reading.read(message);
          simulation.cpy_->on_message(index_, state.index, message);
        }
      }
    }
    void pollWrite(SocketPtr socket, uint32_t = 0) {
      auto &state = tcp_socket_state_.at(socket);
      while (state.writing.size() != 0) {
        size_t available = socket->GetTxAvailable();
        if (available == 0) {
          break;
        }
        writing_.resize(std::min(available, state.writing.size()));
        state.writing.read(writing_);
        auto packet =
            ns3::Create<ns3::Packet>(writing_.data(), writing_.size());
        assert2(socket->Send(packet) == packet->GetSize());
      }
    }

    SocketPtr makeSocket() {
      auto socket = ns3::Socket::CreateSocket(
          GetNode(), ns3::TypeId::LookupByName("ns3::TcpSocketFactory"));
      return socket;
    }

    Index index_;
    SocketPtr tcp_listener_;
    std::unordered_map<Index, SocketInOut> tcp_sockets_;
    std::unordered_map<SocketPtr, SocketState> tcp_socket_state_;
    Bytes writing_;
  };

  ns3::Ptr<Application> Simulation::application(Index index) const {
    return applications_.Get(index)->GetObject<Application>();
  }

  Index add_peer() {
    Index index = simulation.peers_.GetN();
    simulation.peers_.Create(1);
    auto node = simulation.peers_.Get(index);
    simulation.internet_stack_.Install(node);
    simulation.applications_.Add(
        ns3::ApplicationHelper{Application::GetTypeId()}.Install(node));
    simulation.application(index)->index_ = index;
    return index;
  }

  Index add_router() {
    Index index = simulation.routers_.GetN();
    simulation.routers_.Create(1);
    simulation.internet_stack_.Install(simulation.routers_.Get(index));
    return index;
  }

  auto _wire(ns3::Ptr<ns3::Node> node1,
             ns3::Ptr<ns3::Node> node2,
             const WireProps &wire) {
    ns3::PointToPointHelper helper;
    helper.SetDeviceAttribute("DataRate", ns3::DataRateValue{wire.bitrate});
    helper.SetChannelAttribute(
        "Delay", ns3::TimeValue{ns3::MilliSeconds(wire.delay_ms)});
    auto interfaces =
        simulation.address_helper_.Assign(helper.Install(node1, node2));
    simulation.address_helper_.NewNetwork();
    return interfaces.GetAddress(0);
  }

  void wire_peer(Index peer, Index router, const WireProps &wire) {
    assert2(not simulation.ips_.contains(peer));
    auto ip = _wire(
        simulation.peers_.Get(peer), simulation.routers_.Get(router), wire);
    simulation.ips_.emplace(peer, ip);
    simulation.ip_index_.emplace(ip, peer);
  }

  void wire_router(Index router1, Index router2, const WireProps &wire) {
    _wire(simulation.routers_.Get(router1),
          simulation.routers_.Get(router2),
          wire);
  }

  void socket_connect(Index peer1, Index peer2) {
    simulation.application(peer1)->connect(peer2);
  }

  void socket_send(Index peer1, Index peer2, const Message &message) {
    simulation.application(peer1)->send(peer2, message);
  }

  void sleep(uint64_t us, TimerId timer_id) {
    ns3::Simulator::Schedule(ns3::MicroSeconds(us), [timer_id] {
      simulation.cpy_->on_timer(timer_id);
    });
  }

  void run(CPy *cpy, uint32_t timeout_sec) {
    ns3::Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    simulation.applications_.Start(ns3::Seconds(0));
    ns3::Simulator::Stop(ns3::Seconds(timeout_sec));
    simulation.cpy_ = cpy;
    try {
      ns3::Simulator::Run();
    } catch (std::exception &e) {
      printf("catch exception %s\n", e.what());
      exit(-1);
    } catch (...) {
      printf("catch ... %s\n",
             __cxxabiv1::__cxa_current_exception_type()->name());
      exit(-1);
    }
    simulation.cpy_ = nullptr;
    ns3::Simulator::Destroy();
  }
}  // namespace beam_ns3
