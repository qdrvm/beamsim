#ifndef NS3_ASSERT_ENABLE
#define NS3_ASSERT_ENABLE
#endif

#include <arpa/inet.h>
#include <lsquic.h>
#include <ns3/node.h>
#include <ns3/object-factory.h>
#include <ns3/simulator.h>
#include <ns3/socket-factory.h>
#include <ns3/socket.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <lsquic-ns3.hpp>
#include <map>
#include <span>

#include "liblsquic/lsquic_int_types.h"

extern "C" lsquic_time_t lsquic_time_now() {
  return ns3::Simulator::Now().GetMicroSeconds();
}

template <typename T>
inline auto optionTake(std::optional<T> &optional) {
  auto result = std::move(optional);
  optional.reset();
  return result;
}

namespace ns3 {
  constexpr uint32_t kWindowSize = 64 << 10;
  constexpr std::chrono::seconds kIdleTimeout{5};

  struct LsquicCertificate {
    static constexpr std::array<uint8_t, 4> kAlpn{3, 'n', 's', '3'};

    static void setRelativeTime(ASN1_TIME *o_time, auto delta) {
      NS_ASSERT(X509_gmtime_adj(
          o_time,
          std::chrono::duration_cast<std::chrono::seconds>(delta).count()));
    }

    LsquicCertificate() : ctx_{SSL_CTX_new(TLS_method())} {
      static FILE *keylog = [] {
        FILE *f = nullptr;
        if (auto *path = getenv("SSLKEYLOGFILE")) {
          f = fopen(path, "a");
          setvbuf(f, nullptr, _IOLBF, 0);
        }
        return f;
      }();
      if (keylog) {
        SSL_CTX_set_keylog_callback(
            ctx_.get(), +[](const SSL *, const char *line) {
              fputs(line, keylog);
              fputc('\n', keylog);
            });
      }
      NS_ASSERT(SSL_CTX_set_alpn_protos(ctx_.get(), kAlpn.data(), kAlpn.size())
                == 0);
      SSL_CTX_set_alpn_select_cb(ctx_.get(), alpn, this);
      SSL_CTX_set_min_proto_version(ctx_.get(), TLS1_3_VERSION);
      SSL_CTX_set_max_proto_version(ctx_.get(), TLS1_3_VERSION);
      std::array<uint16_t, 1> prefs{SSL_SIGN_ED25519};
      NS_ASSERT(SSL_CTX_set_signing_algorithm_prefs(
          ctx_.get(), prefs.data(), prefs.size()));
      NS_ASSERT(SSL_CTX_set_verify_algorithm_prefs(
          ctx_.get(), prefs.data(), prefs.size()));
      bssl::UniquePtr<X509> x509(X509_new());
      setRelativeTime(X509_getm_notBefore(x509.get()), -std::chrono::days{1});
      setRelativeTime(X509_getm_notAfter(x509.get()), std::chrono::years{1});
      bssl::UniquePtr<EVP_PKEY_CTX> pkey_ctx(
          EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr));
      NS_ASSERT(EVP_PKEY_keygen_init(pkey_ctx.get()));
      EVP_PKEY *_pkey = nullptr;
      NS_ASSERT(EVP_PKEY_keygen(pkey_ctx.get(), &_pkey));
      bssl::UniquePtr<EVP_PKEY> pkey{_pkey};
      NS_ASSERT(SSL_CTX_use_PrivateKey(ctx_.get(), pkey.get()));
      NS_ASSERT(X509_set_pubkey(x509.get(), pkey.get()));
      NS_ASSERT(X509_sign(x509.get(), pkey.get(), nullptr));
      NS_ASSERT(SSL_CTX_use_certificate(ctx_.get(), x509.get()));
    }

    static int alpn(ssl_st *ssl,
                    const unsigned char **out,
                    unsigned char *outlen,
                    const unsigned char *in,
                    unsigned int inlen,
                    void *) {
      uint8_t *out2 = nullptr;
      int r = SSL_select_next_proto(
          &out2, outlen, in, inlen, kAlpn.data(), kAlpn.size());
      *out = out2;
      return r == OPENSSL_NPN_NEGOTIATED ? SSL_TLSEXT_ERR_OK
                                         : SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    bssl::UniquePtr<ssl_ctx_st> ctx_;
  };

  class LsquicSocket;
  class LsquicSocketFactory;

  inline sockaddr toSockaddr(const ns3::Address &address) {
    auto endpoint = InetSocketAddress::ConvertFrom(address);
    sockaddr_in sockaddr_in{
        sizeof(sockaddr_in),
        AF_INET,
        htons(endpoint.GetPort()),
        {htonl(endpoint.GetIpv4().Get())},
        {},
    };
    return reinterpret_cast<const struct sockaddr &>(sockaddr_in);
  }
  inline ns3::Address fromSockaddr(const sockaddr &sockaddr) {
    auto &sockaddr_in = reinterpret_cast<const struct sockaddr_in &>(sockaddr);
    return InetSocketAddress{
        Ipv4Address{ntohl(sockaddr_in.sin_addr.s_addr)},
        ntohs(sockaddr_in.sin_port),
    };
  }

  class LsquicEngine : public Object {
   public:
    LsquicEngine(Ptr<LsquicSocketFactory> factory,
                 const std::optional<Address> &listen);

    ~LsquicEngine() {
      // will call `LsquicEngine::on_conn_closed`, `LsquicEngine::on_close`.
      lsquic_engine_destroy(engine_);
    }

    void pollRead(Ptr<Socket>);

    void pollWrite(Ptr<Socket>, uint32_t) {
      // will call `LsquicEngine::ea_packets_out`.
      lsquic_engine_send_unsent_packets(engine_);
    }

    void process();
    void wantProcess() {
      if (want_process_) {
        return;
      }
      want_process_ = true;
      ns3::Simulator::ScheduleNow([this] { process(); });
    }

    /**
     * Called from `lsquic_engine_connect` (client),
     * `lsquic_engine_process_conns` (server).
     */
    static lsquic_conn_ctx_t *on_new_conn(void *void_self,
                                          lsquic_conn_t *ls_conn);
    /**
     * Called from `lsquic_engine_process_conns`, `lsquic_engine_destroy`.
     */
    static void on_conn_closed(lsquic_conn_t *ls_conn);
    /**
     * Called from `lsquic_engine_packet_in` (client),
     * `on_new_conn` (server).
     */
    static void on_hsk_done(lsquic_conn_t *ls_conn, lsquic_hsk_status status);
    /**
     * Called from `lsquic_conn_make_stream` (client),
     * `lsquic_engine_process_conns` (server).
     */
    static lsquic_stream_ctx_t *on_new_stream(void *void_self,
                                              lsquic_stream_t *ls_stream);
    /**
     * Called from `lsquic_engine_process_conns`, `lsquic_engine_destroy`.
     */
    static void on_close(lsquic_stream_t *ls_stream,
                         lsquic_stream_ctx_t *ls_stream_ctx);
    /**
     * Called from `lsquic_engine_process_conns`.
     * `lsquic_stream_flush` doesn't work inside `on_read`.
     */
    static void on_read(lsquic_stream_t *ls_stream,
                        lsquic_stream_ctx_t *ls_stream_ctx);
    /**
     * Called from `lsquic_engine_process_conns`.
     */
    static void on_write(lsquic_stream_t *ls_stream,
                         lsquic_stream_ctx_t *ls_stream_ctx);
    /**
     * Called from `lsquic_engine_connect` (client),
     * `lsquic_engine_packet_in` (server).
     */
    static ssl_ctx_st *ea_get_ssl_ctx(void *void_self, const sockaddr *);
    /**
     * Called from `lsquic_engine_process_conns`,
     * `lsquic_engine_send_unsent_packets`.
     */
    static int ea_packets_out(void *void_self,
                              const lsquic_out_spec *out_spec,
                              unsigned n_packets_out);

    Ptr<LsquicSocketFactory> factory_;
    Ptr<Socket> socket_;
    sockaddr address_raw_;
    lsquic_engine_t *engine_ = nullptr;
    Ptr<LsquicSocket> listener_;
    std::optional<Ptr<LsquicSocket>> connecting_;
    bool want_process_ = false;
    std::vector<Ptr<LsquicSocket>> want_flush_;
  };

  class LsquicSocketFactory : public SocketFactory {
   public:
    static TypeId GetTypeId() {
      static TypeId tid = TypeId("ns3::LsquicSocketFactory")
                              .SetParent<SocketFactory>()
                              .AddConstructor<LsquicSocketFactory>();
      return tid;
    }

    // Object
    void DoDispose() override;

    // SocketFactory
    Ptr<Socket> CreateSocket() override;

    Ptr<Node> node_;
    Ptr<LsquicEngine> client_;
    std::map<Address, Ptr<LsquicEngine>> servers_;
    std::vector<uint8_t> buffer_;
    LsquicCertificate certificate_;
  };
  NS_OBJECT_ENSURE_REGISTERED(LsquicSocketFactory);

  class LsquicSocket : public Socket {
    friend class LsquicEngine;

   public:
    static TypeId GetTypeId() {
      static TypeId tid = TypeId("ns3::LsquicSocket")
                              .SetParent<Socket>()
                              .AddConstructor<LsquicSocket>();
      return tid;
    }

    // Socket
    SocketErrno GetErrno() const override {
      return errno_;
    }
    SocketType GetSocketType() const override {
      NS_ABORT_MSG("TODO: GetSocketType");
    }
    Ptr<Node> GetNode() const override {
      return factory_->node_;
    }
    int Bind(const Address &address) override {
      NS_ASSERT(not server_.has_value());
      server_.emplace(true);
      NS_ASSERT(not factory_->servers_.contains(address));
      NS_ASSERT(engine_ == nullptr);
      engine_ = Create<LsquicEngine>(factory_, address);
      engine_->listener_ = this;
      factory_->servers_.emplace(address, engine_);
      return 0;
    }
    int Bind() override {
      NS_ABORT_MSG("TODO: Bind");
    }
    int Bind6() override {
      NS_ABORT_MSG("TODO: Bind6");
    }
    int Close() override {
      NS_ABORT_MSG("TODO: Close");
    }
    int ShutdownSend() override {
      NS_ABORT_MSG("TODO: ShutdownSend");
    }
    int ShutdownRecv() override {
      NS_ABORT_MSG("TODO: ShutdownRecv");
    }
    int Connect(const Address &address) override {
      NS_ASSERT(not server_.has_value());
      server_.emplace(false);
      NS_ASSERT(not connecting_);
      connecting_ = true;
      if (factory_->client_ == nullptr) {
        factory_->client_ = Create<LsquicEngine>(factory_, std::nullopt);
      }
      NS_ASSERT(engine_ == nullptr);
      engine_ = factory_->client_;
      NS_ASSERT(not engine_->connecting_.has_value());
      engine_->connecting_.emplace(this);
      auto address_raw = toSockaddr(address);
      // will call `LsquicEngine::ea_get_ssl_ctx`, `LsquicEngine::on_new_conn`.
      lsquic_engine_connect(engine_->engine_,
                            N_LSQVER,
                            &engine_->address_raw_,
                            &address_raw,
                            &*engine_,
                            nullptr,
                            nullptr,
                            0,
                            nullptr,
                            0,
                            nullptr,
                            0);
      engine_->wantProcess();
      if (auto connecting = optionTake(engine_->connecting_)) {
        NS_ABORT_MSG("TODO: lsquic_engine_connect error");
        return -1;
      }
      return 0;
    }
    int Listen() override {
      NS_ASSERT(server_.has_value() and server_.value());
      return 0;
    }
    uint32_t GetTxAvailable() const override {
      NS_ASSERT(server_.has_value() and not server_.value());
      if (ls_stream_ == nullptr) {
        return 0;
      }
      return lsquic_stream_write_avail(ls_stream_);
    }
    int Send(Ptr<Packet> packet, uint32_t flags) override {
      NS_ASSERT(server_.has_value() and not server_.value());
      if (ls_stream_ == nullptr) {
        errno_ = ERROR_AGAIN;
        return -1;
      }
      factory_->buffer_.resize(packet->GetSize());
      packet->CopyData(factory_->buffer_.data(), packet->GetSize());
      auto r = lsquic_stream_write(
          ls_stream_, factory_->buffer_.data(), factory_->buffer_.size());
      if (r == 0) {
        errno_ = ERROR_AGAIN;
        return -1;
      }
      errno_ = ERROR_NOTERROR;
      NS_ASSERT(r > 0);
      if (not want_flush_) {
        want_flush_ = true;
        engine_->want_flush_.emplace_back(this);
        engine_->wantProcess();
      }
      return r;
    }
    int SendTo(Ptr<Packet> p,
               uint32_t flags,
               const Address &toAddress) override {
      NS_ABORT_MSG("TODO: SendTo");
    }
    uint32_t GetRxAvailable() const override {
      NS_ABORT_MSG("TODO: GetRxAvailable");
    }
    Ptr<Packet> Recv(uint32_t maxSize, uint32_t flags) override {
      NS_ASSERT(server_.has_value() and not server_.value());
      NS_ASSERT(ls_stream_ != nullptr);
      maxSize = std::min(maxSize, kWindowSize);
      factory_->buffer_.resize(maxSize);
      auto r = lsquic_stream_read(
          ls_stream_, factory_->buffer_.data(), factory_->buffer_.size());
      if (r == -1 and errno == EWOULDBLOCK) {
        errno_ = ERROR_AGAIN;
        return nullptr;
      }
      errno_ = ERROR_NOTERROR;
      NS_ASSERT(r > 0);
      return Create<Packet>(factory_->buffer_.data(), r);
    }
    Ptr<Packet> RecvFrom(uint32_t maxSize,
                         uint32_t flags,
                         Address &fromAddress) override {
      NS_ABORT_MSG("TODO: RecvFrom");
    }
    int GetSockName(Address &address) const override {
      NS_ABORT_MSG("TODO: GetSockName");
    }
    int GetPeerName(Address &address) const override {
      NS_ABORT_MSG("TODO: GetPeerName");
    }
    bool SetAllowBroadcast(bool allowBroadcast) override {
      NS_ABORT_MSG("TODO: SetAllowBroadcast");
    }
    bool GetAllowBroadcast() const override {
      NS_ABORT_MSG("TODO: GetAllowBroadcast");
    }

    lsquic_conn_ctx_t *connCtx() {
      return reinterpret_cast<lsquic_conn_ctx_t *>(this);
    }
    lsquic_stream_ctx_t *streamCtx() {
      return reinterpret_cast<lsquic_stream_ctx_t *>(this);
    }
    static Ptr<LsquicSocket> from(lsquic_conn_t *ls_conn) {
      return reinterpret_cast<LsquicSocket *>(lsquic_conn_get_ctx(ls_conn));
    }
    static Ptr<LsquicSocket> from(lsquic_stream_ctx_t *ls_stream_ctx) {
      return reinterpret_cast<LsquicSocket *>(ls_stream_ctx);
    }

    Ptr<LsquicSocketFactory> factory_;
    Ptr<LsquicEngine> engine_;
    std::optional<bool> server_;
    bool connecting_ = false;
    lsquic_conn_t *ls_conn_ = nullptr;
    lsquic_stream_t *ls_stream_ = nullptr;
    SocketErrno errno_ = ERROR_NOTERROR;
    bool want_flush_ = false;
  };
  NS_OBJECT_ENSURE_REGISTERED(LsquicSocket);

  void InstallLsquic(const Ptr<Node> &node) {
    auto factory =
        ObjectFactory{"ns3::LsquicSocketFactory"}.Create<LsquicSocketFactory>();
    factory->node_ = node;
    node->AggregateObject(factory);
  }

  LsquicEngine::LsquicEngine(Ptr<LsquicSocketFactory> factory,
                             const std::optional<Address> &listen)
      : factory_{factory} {
    [[maybe_unused]] static auto init = [] {
      NS_ASSERT(lsquic_global_init(LSQUIC_GLOBAL_CLIENT | LSQUIC_GLOBAL_SERVER)
                == 0);
      return 0;
    }();

    static auto tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    socket_ = Socket::CreateSocket(factory_->node_, tid);
    Address address;
    if (listen.has_value()) {
      address = listen.value();
      NS_ASSERT(socket_->Bind(address) == 0);
    } else {
      NS_ASSERT(socket_->Bind() == 0);
      NS_ASSERT(socket_->GetSockName(address) == 0);
    }
    address_raw_ = toSockaddr(address);
    socket_->SetRecvCallback(MakeCallback(&LsquicEngine::pollRead, this));
    socket_->SetSendCallback(MakeCallback(&LsquicEngine::pollWrite, this));

    uint32_t flags = 0;
    if (listen.has_value()) {
      flags |= LSENG_SERVER;
    }

    lsquic_engine_settings settings{};
    lsquic_engine_init_settings(&settings, flags);
    settings.es_init_max_stream_data_bidi_remote = kWindowSize;
    settings.es_init_max_stream_data_bidi_local = kWindowSize;
    settings.es_idle_timeout = kIdleTimeout.count();
    settings.es_versions = LSQUIC_IETF_VERSIONS;

    static lsquic_stream_if stream_if{};
    stream_if.on_new_conn = on_new_conn;
    stream_if.on_conn_closed = on_conn_closed;
    stream_if.on_hsk_done = on_hsk_done;
    stream_if.on_new_stream = on_new_stream;
    stream_if.on_close = on_close;
    stream_if.on_read = on_read;
    stream_if.on_write = on_write;

    lsquic_engine_api api{};
    api.ea_settings = &settings;
    api.ea_stream_if = &stream_if;
    api.ea_stream_if_ctx = this;
    api.ea_packets_out = ea_packets_out;
    api.ea_packets_out_ctx = this;
    api.ea_get_ssl_ctx = ea_get_ssl_ctx;

    engine_ = lsquic_engine_new(flags, &api);
    NS_ASSERT(engine_ != nullptr);
  }

  void LsquicEngine::pollRead(Ptr<Socket>) {
    Address from_address;
    while (auto packet = socket_->RecvFrom(from_address)) {
      factory_->buffer_.resize(packet->GetSize());
      packet->CopyData(factory_->buffer_.data(), packet->GetSize());
      auto from_address_raw = toSockaddr(from_address);
      // will call `LsquicEngine::on_hsk_done`, `LsquicEngine::ea_get_ssl_ctx`.
      lsquic_engine_packet_in(engine_,
                              factory_->buffer_.data(),
                              packet->GetSize(),
                              &address_raw_,
                              &from_address_raw,
                              this,
                              0);
    }

    process();
  }

  void LsquicEngine::process() {
    want_process_ = false;
    while (not want_flush_.empty()) {
      auto socket = want_flush_.back();
      want_flush_.pop_back();
      socket->want_flush_ = false;
      NS_ASSERT(socket->ls_stream_ != nullptr);
      lsquic_stream_flush(socket->ls_stream_);
    }
    // will call `LsquicEngine::on_new_conn`, `LsquicEngine::on_conn_closed`,
    // `LsquicEngine::on_new_stream`, `LsquicEngine::on_close`, `LsquicEngine::on_read`,
    // `LsquicEngine::on_write`, `LsquicEngine::ea_packets_out`.
    lsquic_engine_process_conns(engine_);
    int us = 0;
    if (not lsquic_engine_earliest_adv_tick(engine_, &us)) {
      return;
    }
    if (us <= 0) {
      return;
    }
    ns3::Simulator::Schedule(ns3::MicroSeconds(us), [this] { process(); });
  }

  lsquic_conn_ctx_t *LsquicEngine::on_new_conn(void *void_self,
                                               lsquic_conn_t *ls_conn) {
    LsquicEngine *self = static_cast<LsquicEngine *>(void_self);
    auto connecting = optionTake(self->connecting_);
    auto is_connecting = connecting.has_value();
    Ptr<LsquicSocket> socket;
    if (connecting.has_value()) {
      socket = connecting.value();
    } else {
      socket = Create<LsquicSocket>();
      socket->factory_ = self->factory_;
      socket->engine_ = self;
      socket->server_.emplace(false);
    }
    socket->ls_conn_ = ls_conn;
    socket->Ref();
    lsquic_conn_set_ctx(ls_conn, socket->connCtx());
    if (not is_connecting) {
      // lsquic doesn't call `on_hsk_done` for incoming connection
      on_hsk_done(ls_conn, LSQ_HSK_OK);
    }
    return socket->connCtx();
  }

  void LsquicEngine::on_conn_closed(lsquic_conn_t *ls_conn) {
    auto socket = LsquicSocket::from(ls_conn);
    lsquic_conn_set_ctx(ls_conn, nullptr);
    if (socket->connecting_) {
      socket->connecting_ = false;
      socket->NotifyConnectionFailed();
    } else {
      socket->NotifyNormalClose();
    }
    socket->ls_conn_ = nullptr;
    socket->Unref();
  }

  void LsquicEngine::on_hsk_done(lsquic_conn_t *ls_conn,
                                 lsquic_hsk_status status) {
    auto socket = LsquicSocket::from(ls_conn);
    auto ok = status == LSQ_HSK_OK or status == LSQ_HSK_RESUMED_OK;
    if (not ok) {
      lsquic_conn_close(ls_conn);
    }
    if (socket->connecting_) {
      if (not ok) {
        socket->connecting_ = false;
        socket->NotifyConnectionFailed();
      } else {
        NS_ASSERT(socket->ls_stream_ == nullptr);
        // will call `Engine::on_new_stream`.
        lsquic_conn_make_stream(socket->ls_conn_);
        NS_ASSERT(not socket->connecting_);
        NS_ASSERT(socket->ls_stream_ != nullptr);
      }
    }
  }

  lsquic_stream_ctx_t *LsquicEngine::on_new_stream(void *,
                                                   lsquic_stream_t *ls_stream) {
    auto socket = LsquicSocket::from(lsquic_stream_conn(ls_stream));
    NS_ASSERT(socket->ls_stream_ == nullptr);
    socket->ls_stream_ = ls_stream;
    socket->Ref();
    lsquic_stream_set_ctx(ls_stream, socket->streamCtx());
    lsquic_stream_wantread(ls_stream, 1);
    lsquic_stream_wantwrite(ls_stream, 1);
    if (socket->connecting_) {
      socket->connecting_ = false;
      socket->NotifyConnectionSucceeded();
    } else {
      const sockaddr *local_address_raw = nullptr;
      const sockaddr *address_raw = nullptr;
      lsquic_conn_get_sockaddr(
          socket->ls_conn_, &local_address_raw, &address_raw);
      socket->engine_->listener_->NotifyNewConnectionCreated(
          socket, fromSockaddr(*address_raw));
    }
    return socket->streamCtx();
  }

  void LsquicEngine::on_close(lsquic_stream_t *ls_stream,
                              lsquic_stream_ctx_t *ls_stream_ctx) {
    auto socket = LsquicSocket::from(ls_stream_ctx);
    NS_ABORT_MSG("TODO: notify close");
    socket->ls_stream_ = nullptr;
    socket->Unref();
  }

  void LsquicEngine::on_read(lsquic_stream_t *ls_stream,
                             lsquic_stream_ctx_t *ls_stream_ctx) {
    auto socket = LsquicSocket::from(ls_stream_ctx);
    socket->NotifyDataRecv();
  }

  void LsquicEngine::on_write(lsquic_stream_t *ls_stream,
                              lsquic_stream_ctx_t *ls_stream_ctx) {
    auto socket = LsquicSocket::from(ls_stream_ctx);
    socket->NotifySend(socket->GetTxAvailable());
  }

  ssl_ctx_st *LsquicEngine::ea_get_ssl_ctx(void *void_self, const sockaddr *) {
    LsquicEngine *self = static_cast<LsquicEngine *>(void_self);
    return self->factory_->certificate_.ctx_.get();
  }

  int LsquicEngine::ea_packets_out(void *void_self,
                                   const lsquic_out_spec *out_spec,
                                   unsigned n_packets_out) {
    LsquicEngine *self = static_cast<LsquicEngine *>(void_self);
    int packets = 0;
    for (auto &spec : std::span{out_spec, n_packets_out}) {
      auto address = fromSockaddr(*spec.dest_sa);
      auto packet = Create<Packet>();
      for (auto &iovec : std::span{spec.iov, spec.iovlen}) {
        packet->AddAtEnd(Create<Packet>(
            static_cast<const uint8_t *>(iovec.iov_base), iovec.iov_len));
      }
      auto r = self->socket_->SendTo(packet, 0, address);
      NS_ASSERT(r == packet->GetSize());
      ++packets;
    }
    return packets;
  }

  void LsquicSocketFactory::DoDispose() {
    node_ = nullptr;
    client_ = nullptr;
    servers_.clear();
    SocketFactory::DoDispose();
  }

  Ptr<Socket> LsquicSocketFactory::CreateSocket() {
    auto socket = Create<LsquicSocket>();
    socket->factory_ = this;
    return socket;
  }
}  // namespace ns3
