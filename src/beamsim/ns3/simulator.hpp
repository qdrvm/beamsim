#pragma once

#include <ns3/applications-module.h>
#include <ns3/core-module.h>
#include <ns3/internet-module.h>
#include <ns3/network-module.h>
#include <ns3/point-to-point-helper.h>

#include <beamsim/assert.hpp>
#include <beamsim/i_simulator.hpp>
#include <span>
#include <unordered_map>

namespace beamsim::ns3_ {
  using MessageId = uint64_t;
  using SocketPtr = ns3::Ptr<ns3::Socket>;
  using Bytes = std::vector<uint8_t>;
  using BytesIn = std::span<const uint8_t>;
  using BytesOut = std::span<uint8_t>;
  template <size_t N>
  using BytesN = std::array<uint8_t, N>;

  struct WireProps {
    uint64_t bitrate;
    uint32_t delay_ms;
  };

  void append(Bytes &l, auto &&r) {
    l.insert(l.end(), r.begin(), r.end());
  }

  constexpr uint16_t kPort = 10000;

  inline ns3::Time timeToNs3(Time time) {
    static_assert(std::is_same_v<Time, std::chrono::microseconds>);
    return ns3::MicroSeconds(time.count());
  }

  template <std::integral T>
  struct encode_ne {
    BytesN<sizeof(T)> bytes{};
    encode_ne() = default;
    encode_ne(T v) {
      memcpy(bytes.data(), &v, sizeof(T));
    }
    T value() const {
      T v;
      memcpy(&v, bytes.data(), sizeof(T));
      return v;
    }
  };

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
  };

  struct Header {
    MessageSize size() const {
      return message_size.bytes.size() + message_id.bytes.size()
           + data_size.bytes.size();
    }

    encode_ne<MessageSize> message_size;
    encode_ne<MessageId> message_id;
    encode_ne<MessageSize> data_size;
  };

  struct Reading {
    struct Frame {
      Header header;
      MessageSize padding;
    };

    Buffer buffer_;
    std::optional<Frame> frame_;
  };

  struct Writing {
    struct Item {
      Bytes data;
      MessageSize size;
      MessageSize offset = 0;
    };

    bool empty() const {
      return queue_.empty();
    }

    void write(MessageId message_id, const IMessage &message) {
      // TODO: encode message
      Item item;
      auto message_size = message.size();
      Header header;
      header.message_size = message_size;
      header.message_id = message_id;
      header.data_size = 0;
      item.size = header.size() + message_size;
      append(item.data, header.message_size.bytes);
      append(item.data, header.message_id.bytes);
      append(item.data, header.data_size.bytes);
      queue_.emplace_back(std::move(item));
    }

    ns3::Ptr<ns3::Packet> readPacket(uint32_t available) {
      auto packet = ns3::Create<ns3::Packet>();
      while (available != 0 and not queue_.empty()) {
        auto &item = queue_.front();
        if (item.offset < item.data.size()) {
          auto n =
              std::min<uint32_t>(available, item.data.size() - item.offset);
          packet->AddAtEnd(
              ns3::Create<ns3::Packet>(item.data.data() + item.offset, n));
          item.offset += n;
          available -= n;
        } else if (item.offset < item.size) {
          auto n = std::min<uint32_t>(available, item.size - item.offset);
          packet->AddPaddingAtEnd(n);
          item.offset += n;
          available -= n;
        } else {
          queue_.pop_front();
        }
      }
      return packet;
    }

    std::deque<Item> queue_;
  };

  struct SocketState {
    PeerIndex peer_index;
    Reading reading{};
    Writing writing{};
  };

  struct SocketInOut {
    SocketPtr in;
    SocketPtr out;
    bool write_out = false;

    auto &write() const {
      return write_out ? out : in;
    }
  };

  class Simulator;
  class Application : public ns3::Application {
   public:
    Application(Simulator &simulator, std::unique_ptr<IPeer> &&peer)
        : simulator_{simulator}, peer_{std::move(peer)} {}

    // Application
    void StartApplication() override {
      listen();
      peer_->onStart();
    }

    SocketPtr makeSocket() {
      auto socket = ns3::Socket::CreateSocket(
          GetNode(), ns3::TypeId::LookupByName("ns3::TcpSocketFactory"));
      return socket;
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
    void onAccept(SocketPtr socket, const ns3::Address &address);
    void add(PeerIndex peer_index, SocketPtr socket) {
      socket->SetRecvCallback(MakeCallback(&Application::pollRead, this));
      socket->SetSendCallback(MakeCallback(&Application::pollWrite, this));
      tcp_socket_state_.emplace(socket, SocketState{peer_index});
    }
    void pollRead(SocketPtr socket);
    void pollWrite(SocketPtr socket, uint32_t = 0) {
      auto &state = tcp_socket_state_.at(socket);
      while (not state.writing.empty()) {
        size_t available = socket->GetTxAvailable();
        if (available == 0) {
          break;
        }
        auto packet = state.writing.readPacket(available);
        assert2(socket->Send(packet) == static_cast<int>(packet->GetSize()));
      }
    }
    void connect(PeerIndex peer_index);
    void send(PeerIndex peer_index,
              MessageId message_id,
              const IMessage &message) {
      assert2(peer_index != peer_->peer_index_);
      auto &sockets = tcp_sockets_[peer_index];
      if (not sockets.write()) {
        connect(peer_index);
      }
      auto &socket = sockets.write();
      auto &state = tcp_socket_state_.at(socket);
      state.writing.write(message_id, message);
      pollWrite(socket);
    }

    Simulator &simulator_;
    std::unique_ptr<IPeer> peer_;
    SocketPtr tcp_listener_;
    std::unordered_map<PeerIndex, SocketInOut> tcp_sockets_;
    std::unordered_map<SocketPtr, SocketState> tcp_socket_state_;
    Bytes reading_;
  };

  class Simulator : public ISimulator {
   public:
    Simulator() {
      address_helper_.SetBase("10.1.1.0", "255.255.255.0");
    }

    // ISimulator
    ~Simulator() override {
      ns3::Simulator::Destroy();
    }
    Time time() const override {
      static_assert(std::is_same_v<Time, std::chrono::microseconds>);
      return Time{ns3::Simulator::Now().GetMicroSeconds()};
    }
    void stop() override {
      ns3::Simulator::Stop();
    }
    void runAt(Time time, OnTimer &&on_timer) override {
      ns3::Simulator::Schedule(timeToNs3(time) - ns3::Simulator::Now(),
                               std::move(on_timer));
    }
    void runSoon(OnTimer &&on_timer) override {
      ns3::Simulator::ScheduleNow(std::move(on_timer));
    }
    void runAfter(Time delay, OnTimer &&on_timer) override {
      ns3::Simulator::Schedule(timeToNs3(delay), std::move(on_timer));
    }
    void send(PeerIndex from_peer,
              PeerIndex to_peer,
              MessagePtr message) override {
      auto message_id = next_message_id_;
      messages_.emplace(message_id, message);
      ++next_message_id_;
      applications_.at(from_peer)->send(to_peer, message_id, *message);
    }
    void _receive(PeerIndex, PeerIndex, MessagePtr) override {
      abort();
    }


    template <typename Peer, typename... A>
    void addPeer(A &&...a) {
      PeerIndex index = applications_.size();
      assert2(applications_.size() < peers_.GetN());
      auto peer = std::make_unique<Peer>(*this, index, std::forward<A>(a)...);
      auto application = ns3::Create<Application>(*this, std::move(peer));
      applications_.emplace_back(application);
      peers_.Get(index)->AddApplication(std::move(application));
    }
    template <typename Peer, typename... A>
    void addPeers(PeerIndex count, A &&...a) {
      for (PeerIndex i = 0; i < count; ++i) {
        addPeer<Peer>(std::forward<A>(a)...);
      }
    }

    IPeer &peer(PeerIndex peer_index) const {
      return *applications_.at(peer_index)->peer_;
    }

    PeerIndex addPeerNode() {
      PeerIndex index = peers_.GetN();
      peers_.Create(1);
      internet_stack_.Install(peers_.Get(index));
      return index;
    }

    PeerIndex addRouter() {
      PeerIndex index = routers_.GetN();
      routers_.Create(1);
      internet_stack_.Install(routers_.Get(index));
      return index;
    }

    auto _wire(ns3::Ptr<ns3::Node> node1,
               ns3::Ptr<ns3::Node> node2,
               const WireProps &wire) {
      ns3::PointToPointHelper helper;
      helper.SetDeviceAttribute("DataRate", ns3::DataRateValue{wire.bitrate});
      helper.SetChannelAttribute(
          "Delay", ns3::TimeValue{ns3::MilliSeconds(wire.delay_ms)});
      auto interfaces = address_helper_.Assign(helper.Install(node1, node2));
      address_helper_.NewNetwork();
      return interfaces.GetAddress(0);
    }

    void wirePeer(PeerIndex peer, PeerIndex router, const WireProps &wire) {
      assert2(not ips_.contains(peer));
      auto ip = _wire(peers_.Get(peer), routers_.Get(router), wire);
      ips_.emplace(peer, ip);
      ip_peer_index_.emplace(ip, peer);
    }

    void wireRouter(PeerIndex router1,
                    PeerIndex router2,
                    const WireProps &wire) {
      _wire(routers_.Get(router1), routers_.Get(router2), wire);
    }

    void run(Time timeout) {
      using C = std::chrono::steady_clock;
      auto t0 = C::now();
      ns3::Ipv4GlobalRoutingHelper::PopulateRoutingTables();
      auto dt =
          std::chrono::duration_cast<std::chrono::milliseconds>(C::now() - t0);
      std::println("ns3::Ipv4GlobalRoutingHelper::PopulateRoutingTables {}ms",
                   dt.count());

      ns3::Simulator::Stop(timeToNs3(timeout));
      ns3::Simulator::Run();
      ns3::Simulator::Stop();
    }

    ns3::InternetStackHelper internet_stack_;
    ns3::Ipv4AddressHelper address_helper_;
    ns3::NodeContainer peers_;
    ns3::NodeContainer routers_;
    std::vector<ns3::Ptr<Application>> applications_;
    std::unordered_map<PeerIndex, ns3::Ipv4Address> ips_;
    std::unordered_map<ns3::Ipv4Address, PeerIndex, ns3::Ipv4AddressHash>
        ip_peer_index_;
    MessageId next_message_id_ = 0;
    std::unordered_map<MessageId, MessagePtr> messages_;
  };

  void Application::onAccept(SocketPtr socket, const ns3::Address &address) {
    auto index = simulator_.ip_peer_index_.at(
        ns3::InetSocketAddress::ConvertFrom(address).GetIpv4());
    auto &sockets = tcp_sockets_[index];
    assert2(not sockets.in);
    sockets.in = socket;
    sockets.write_out = (bool)sockets.out;
    add(index, socket);
  }

  void Application::pollRead(SocketPtr socket) {
    auto &state = tcp_socket_state_.at(socket);
    while (auto packet = socket->Recv()) {
      reading_.resize(packet->GetSize());
      packet->CopyData(reading_.data(), reading_.size());
      state.reading.buffer_.write(reading_);
      while (true) {
        if (state.reading.frame_.has_value()) {
          // TODO: read data
          if (state.reading.frame_->padding > 0) {
            auto n = std::min<MessageSize>(state.reading.frame_->padding,
                                           state.reading.buffer_.size());
            if (n == 0) {
              break;
            }
            state.reading.buffer_.read(n);
            state.reading.frame_->padding -= n;
            continue;
          }
          auto item = std::exchange(state.reading.frame_, {}).value();
          // TODO: decode message
          auto node =
              simulator_.messages_.extract(item.header.message_id.value());
          assert2(node);
          peer_->onMessage(state.peer_index, std::move(node.mapped()));
          continue;
        }
        Header header;
        if (state.reading.buffer_.size() < header.size()) {
          break;
        }
        state.reading.buffer_.read(header.message_size.bytes);
        state.reading.buffer_.read(header.message_id.bytes);
        state.reading.buffer_.read(header.data_size.bytes);
        state.reading.frame_.emplace(
            header, header.message_size.value() - header.data_size.value());
      }
    }
  }

  void Application::connect(PeerIndex peer_index) {
    auto &sockets = tcp_sockets_[peer_index];
    assert2(not sockets.out);
    if (sockets.write()) {
      return;
    }
    auto socket = makeSocket();
    socket->Connect(
        ns3::InetSocketAddress{simulator_.ips_.at(peer_index), kPort});
    sockets.out = socket;
    sockets.write_out = true;
    add(peer_index, socket);
  }
}  // namespace beamsim::ns3_
