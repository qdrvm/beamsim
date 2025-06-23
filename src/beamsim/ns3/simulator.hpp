#pragma once

#include <ns3/applications-module.h>
#include <ns3/core-module.h>
#include <ns3/internet-module.h>
#include <ns3/mpi-interface.h>
#include <ns3/network-module.h>

#include <beamsim/i_simulator.hpp>
#include <beamsim/ns3/protocol.hpp>
#include <beamsim/ns3/routing.hpp>
#include <unordered_map>

namespace beamsim::ns3_ {
  using MessageId = uint64_t;
  using SocketPtr = ns3::Ptr<ns3::Socket>;
  using BytesOut = std::span<uint8_t>;

  constexpr uint16_t kPort = 10000;

  inline ns3::Time timeToNs3(Time time) {
    static_assert(std::is_same_v<Time, std::chrono::microseconds>);
    return ns3::MicroSeconds(time.count());
  }

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
      Bytes data;
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

    void write(std::optional<MessageId> message_id, const IMessage &message) {
      Item item;
      auto message_data = message.encode();
      auto message_size = message_data.size() + message.padding();
      Header header;
      header.message_size = message_size;
      header.message_id = message_id.value_or(0);
      header.data_size = message_data.size();
      item.size = header.size() + message_size;
      append(item.data, header.message_size.bytes);
      append(item.data, header.message_id.bytes);
      append(item.data, header.data_size.bytes);
      append(item.data, message_data);
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

    SocketPtr makeSocket();
    void listen();
    void onUdpReceive(SocketPtr socket);
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
    void onConnect(SocketPtr socket) {
      pollWrite(socket);
    }
    void onConnectError(SocketPtr) {
      abort();
    }
    void connect(PeerIndex peer_index);
    void send(PeerIndex peer_index,
              std::optional<MessageId> message_id,
              const IMessage &message);

   private:
    void sendTcp(PeerIndex peer_index,
                 std::optional<MessageId> message_id,
                 const IMessage &message);

    void sendUdp(PeerIndex peer_index,
                 std::optional<MessageId> message_id,
                 const IMessage &message);

   public:
    Simulator &simulator_;
    std::unique_ptr<IPeer> peer_;
   private:
    SocketPtr tcp_listener_;
    std::unordered_map<PeerIndex, SocketInOut> tcp_sockets_;
    std::unordered_map<SocketPtr, SocketState> tcp_socket_state_;
    Bytes reading_;
  };

  class Simulator : public ISimulator {
   public:
    Simulator(IMetrics *metrics) : metrics_{metrics} {
      if (mpiSize() > 0) {
        ns3::MpiInterface::Enable(MPI_COMM_WORLD);
      }
    }

    void setProtocol(Protocol protocol) {
      protocol_ = protocol;
    }

    // ISimulator
    ~Simulator() override {
      ns3::Simulator::Destroy();
      if (mpiSize() > 0) {
        ns3::MpiInterface::Disable();
      }
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
      if (metrics_ != nullptr) {
        metrics_->onPeerSentMessage(from_peer);
      }
      std::optional<MessageId> message_id;
      if (cache_messages_ and isLocalPeer(to_peer)) {
        message_id = next_message_id_;
        messages_.emplace(message_id.value(), message);
        ++next_message_id_;
      }
      applications_.at(from_peer)->send(to_peer, message_id, *message);
    }
    void _receive(PeerIndex, PeerIndex, MessagePtr) override {
      abort();
    }
    void connect(PeerIndex peer1, PeerIndex peer2) override {
      routing_.connect(peer1, peer2);
    }

    template <typename Peer, typename... A>
    void addPeer(A &&...a) {
      PeerIndex index = applications_.size();
      assert2(applications_.size() < routing_.peers_.GetN());
      auto &application = applications_.emplace_back();
      if (isLocalPeer(index)) {
        auto peer = std::make_unique<Peer>(*this, index, std::forward<A>(a)...);
        application = ns3::Create<Application>(*this, std::move(peer));
        routing_.peers_.Get(index)->AddApplication(std::move(application));
      }
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

    void run(Time timeout) {
      traceMetrics();
      routing_.populateRoutingTables();

      ns3::Simulator::Stop(timeToNs3(timeout));
      ns3::Simulator::Run();
      ns3::Simulator::Stop();
    }

    void traceMetrics() {
      if (metrics_ == nullptr) {
        return;
      }
      for (PeerIndex i = 0; i < routing_.peers_.GetN(); ++i) {
        if (not isLocalPeer(i)) {
          continue;
        }
        auto node = routing_.peers_.Get(i);
        for (uint32_t j = 0; j < node->GetNDevices(); ++j) {
          auto dev = node->GetDevice(j);
          if (not dev->IsPointToPoint()) {
            continue;
          }
          dev->TraceConnectWithoutContext(
              "MacRx",
              ns3::MakeCallback(&Simulator::traceOnPeerReceivedBytes, this, i));
          dev->TraceConnectWithoutContext(
              "MacTx",
              ns3::MakeCallback(&Simulator::traceOnPeerSentBytes, this, i));
        }
      }
    }

    void traceOnPeerReceivedBytes(PeerIndex peer_index,
                                  ns3::Ptr<const ns3::Packet> packet) {
      metrics_->onPeerReceivedBytes(peer_index, packet->GetSize());
    }

    void traceOnPeerSentBytes(PeerIndex peer_index,
                              ns3::Ptr<const ns3::Packet> packet) {
      metrics_->onPeerSentBytes(peer_index, packet->GetSize());
    }

    bool isLocalPeer(PeerIndex peer_index) const {
      return routing_.peers_.Get(peer_index)->GetSystemId() == mpiIndex();
    }

    IMetrics *metrics_;
    Protocol protocol_ = Protocol::TCP;
    bool cache_messages_ = true;
    std::vector<ns3::Ptr<Application>> applications_;
    Routing routing_;
    MessageId next_message_id_ = 0;
    std::unordered_map<MessageId, MessagePtr> messages_;
    MessageDecodeFn message_decode_;
  };

  // Application method implementations
  inline SocketPtr Application::makeSocket() {
    const char* socketFactory = simulator_.protocol_ == Protocol::UDP ? 
                                 "ns3::UdpSocketFactory" : "ns3::TcpSocketFactory";
    auto socket = ns3::Socket::CreateSocket(
        GetNode(), ns3::TypeId::LookupByName(socketFactory));
    return socket;
  }

  inline void Application::listen() {
    if (simulator_.protocol_ == Protocol::UDP) {
      tcp_listener_ = makeSocket();
      tcp_listener_->Bind(ns3::InetSocketAddress{
          ns3::Ipv4Address::GetAny(),
          kPort,
      });
      tcp_listener_->SetRecvCallback(ns3::MakeCallback(&Application::onUdpReceive, this));
    } else {
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
  }

  inline void Application::send(PeerIndex peer_index,
                                std::optional<MessageId> message_id,
                                const IMessage &message) {
    assert2(peer_index != peer_->peer_index_);
    
    if (simulator_.protocol_ == Protocol::UDP) {
      sendUdp(peer_index, message_id, message);
    } else {
      sendTcp(peer_index, message_id, message);
    }
  }

  inline void Application::sendTcp(PeerIndex peer_index,
                                   std::optional<MessageId> message_id,
                                   const IMessage &message) {
    auto &sockets = tcp_sockets_[peer_index];
    auto connected = sockets.write() != nullptr;
    if (not connected) {
      connect(peer_index);
    }
    auto &socket = sockets.write();
    auto &state = tcp_socket_state_.at(socket);
    state.writing.write(message_id, message);
    if (connected) {
      pollWrite(socket);
    }
  }

  inline void Application::sendUdp(PeerIndex peer_index,
                                   std::optional<MessageId> message_id,
                                   const IMessage &message) {
    // For UDP, create a simple packet with the serialized message
    Bytes data;
    MessageEncodeTo encode_data{[&data](BytesIn part) { 
      data.insert(data.end(), part.begin(), part.end()); 
    }};
    message.encode(encode_data);
    
    auto packet = ns3::Create<ns3::Packet>(data.data(), data.size());
    
    // Send directly to the peer
    auto socket = makeSocket();
    socket->Connect(ns3::InetSocketAddress{
        simulator_.routing_.peer_ips_.at(peer_index), kPort});
    socket->Send(packet);
    socket->Close();
  }

  inline void Application::connect(PeerIndex peer_index) {
    if (simulator_.protocol_ == Protocol::UDP) {
      return; // UDP is connectionless, no need to connect
    }
    
    auto &sockets = tcp_sockets_[peer_index];
    assert2(not sockets.out);
    if (sockets.write()) {
      return;
    }
    auto socket = makeSocket();
    socket->Connect(ns3::InetSocketAddress{
        simulator_.routing_.peer_ips_.at(peer_index), kPort});
    sockets.out = socket;
    sockets.write_out = true;
    socket->SetConnectCallback(
        MakeCallback(&Application::onConnect, this),
        MakeCallback(&Application::onConnectError, this));
    add(peer_index, socket);
  }

  void Application::onAccept(SocketPtr socket, const ns3::Address &address) {
    auto index = simulator_.routing_.ip_peer_index_.at(
        ns3::InetSocketAddress::ConvertFrom(address).GetIpv4());
    auto &sockets = tcp_sockets_[index];
    assert2(not sockets.in);
    sockets.in = socket;
    sockets.write_out = (bool)sockets.out;
    add(index, socket);
  }

  void Application::onUdpReceive(SocketPtr socket) {
    ns3::Address from_address;
    auto packet = socket->RecvFrom(from_address);
    if (!packet) {
      return;
    }

    auto from_ip = ns3::InetSocketAddress::ConvertFrom(from_address).GetIpv4();
    auto it = simulator_.routing_.ip_peer_index_.find(from_ip);
    if (it == simulator_.routing_.ip_peer_index_.end()) {
      return; // Unknown sender
    }
    auto from_peer = it->second;

    // Extract message data
    auto size = packet->GetSize();
    Bytes data(size);
    packet->CopyData(data.data(), size);

    // Decode message
    MessageDecodeFrom decode_data{data};
    auto message = simulator_.message_decode_(decode_data);

    if (simulator_.metrics_ != nullptr) {
      simulator_.metrics_->onPeerReceivedMessage(peer_->peer_index_);
    }

    peer_->onMessage(from_peer, std::move(message));
  }

  void Application::pollRead(SocketPtr socket) {
    auto &state = tcp_socket_state_.at(socket);
    while (auto packet = socket->Recv()) {
      reading_.resize(packet->GetSize());
      packet->CopyData(reading_.data(), reading_.size());
      state.reading.buffer_.write(reading_);
      while (true) {
        auto &buffer = state.reading.buffer_;
        if (state.reading.frame_.has_value()) {
          auto &frame = state.reading.frame_.value();
          auto want_data = frame.header.data_size.value() - frame.data.size();
          if (want_data > 0) {
            auto n = std::min<MessageSize>(want_data, buffer.size());
            if (n == 0) {
              break;
            }
            frame.data.resize(frame.data.size() + n);
            buffer.peek(std::span{frame.data}.subspan(frame.data.size() - n));
            buffer.read(n);
            continue;
          }
          if (frame.padding > 0) {
            auto n = std::min<MessageSize>(frame.padding, buffer.size());
            if (n == 0) {
              break;
            }
            buffer.read(n);
            frame.padding -= n;
            continue;
          }
          auto item = std::exchange(state.reading.frame_, {}).value();
          MessagePtr message;
          if (simulator_.cache_messages_
              and simulator_.isLocalPeer(state.peer_index)) {
            auto node =
                simulator_.messages_.extract(item.header.message_id.value());
            assert2(node);
            message = std::move(node.mapped());
          } else {
            MessageDecodeFrom data{item.data};
            message = simulator_.message_decode_(data);
          }
          if (simulator_.metrics_ != nullptr) {
            simulator_.metrics_->onPeerReceivedMessage(state.peer_index);
          }
          peer_->onMessage(state.peer_index, std::move(message));
          continue;
        }
        Header header;
        if (buffer.size() < header.size()) {
          break;
        }
        buffer.read(header.message_size.bytes);
        buffer.read(header.message_id.bytes);
        buffer.read(header.data_size.bytes);
        state.reading.frame_.emplace(
            header,
            Bytes{},
            header.message_size.value() - header.data_size.value());
      }
    }
  }
}  // namespace beamsim::ns3_
