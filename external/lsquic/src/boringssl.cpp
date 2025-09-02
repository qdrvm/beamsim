#include <ns3/assert.h>

#include <openssl/boringssl.hpp>
#include <print>
#include <random>
#include <span>

template <size_t line>
struct LineStub {
  template <typename T>
  operator T() {
    return (T)line;
  }
};
#define LINE_STUB \
  LineStub<__LINE__> {}

#define CONST(x) x

#define TODO std::println("TODO: {}", __FUNCTION__), abort()

#define IQUIC_TAG_LEN 16

struct ssl_st {
  enum State {
    INIT,
    CLIENT_CONNECT,
    CLIENT_WRITE_L0,
     SERVER_READ_L0,
    SERVER_WRITE_L0,
     CLIENT_READ_L0,
    CLIENT_SET_WL2_RL2,
    SERVER_SET_TP,
    SERVER_WRITE_L2,
     CLIENT_READ_L2,
    CLIENT_WRITE_L2,
     SERVER_READ_L2,
    SERVER_WRITE_L3,
    ESTABLISHED,
  };

  void setWriteSecret(ssl_encryption_level_t level) {
    uint8_t secret[32]{};
    io->set_write_secret(this, level, nullptr, secret, sizeof(secret));
  }

  void setReadSecret(ssl_encryption_level_t level) {
    uint8_t secret[32]{};
    io->set_read_secret(this, level, nullptr, secret, sizeof(secret));
  }

  std::optional<bool> client;
  std::optional<void *> ex_data;
  const SSL_QUIC_METHOD *io = nullptr;
  std::optional<std::vector<uint8_t>> transport_params;
  std::optional<std::vector<uint8_t>> peer_transport_params;
  State state = INIT;
};

inline auto &randomEngine() {
  static std::default_random_engine random_engine;
  return random_engine;
}

inline void mix(std::span<uint8_t> out, std::span<const uint8_t> in) {
  auto n = std::max(out.size(), in.size());
  for (size_t i = 0; i < n; ++i) {
    out[i % out.size()] ^= in[i % in.size()];
  }
}

// clang-format off

OPENSSL_EXPORT int RAND_bytes(uint8_t *buf, size_t len) {
  for (auto &byte : std::span{buf, len}) {
    byte = std::uniform_int_distribution<uint8_t>{}(randomEngine());
  }
  return true;
}

OPENSSL_EXPORT int SHA256_Init(SHA256_CTX *sha) { TODO; }

OPENSSL_EXPORT int SHA256_Update(SHA256_CTX *sha, const void *data, size_t len) { TODO; }

OPENSSL_EXPORT int SHA256_Final(uint8_t out[SHA256_DIGEST_LENGTH], SHA256_CTX *sha) { TODO; }

OPENSSL_EXPORT uint8_t *HMAC(const EVP_MD *evp_md, const void *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out, unsigned int *out_len) { TODO; }

OPENSSL_EXPORT const EVP_MD *EVP_sha256(void) {
  return LINE_STUB;
}

OPENSSL_EXPORT int HKDF_expand(uint8_t *out_key, size_t out_len, const EVP_MD *digest, const uint8_t *prk, size_t prk_len, const uint8_t *info, size_t info_len) {
  memset(out_key, 0, out_len);
  mix({out_key, out_len}, {prk, prk_len});
  mix({out_key, out_len}, {info, info_len});
  return true;
}

OPENSSL_EXPORT void X25519_public_from_private(uint8_t out_public_value[32], const uint8_t private_key[32]) { TODO; }

OPENSSL_EXPORT int X25519(uint8_t out_shared_key[32], const uint8_t private_key[32], const uint8_t peer_public_value[32]) { TODO; }

OPENSSL_EXPORT int EVP_AEAD_CTX_seal(const EVP_AEAD_CTX *ctx, uint8_t *out, size_t *out_len, size_t max_out_len, const uint8_t *nonce, size_t nonce_len, const uint8_t *in, size_t in_len, const uint8_t *ad, size_t ad_len) {
  NS_ASSERT(max_out_len > IQUIC_TAG_LEN);
  *out_len = std::min(in_len + IQUIC_TAG_LEN, max_out_len);
  memcpy(out, in, *out_len - IQUIC_TAG_LEN);
  memset(out + *out_len - IQUIC_TAG_LEN, 0, IQUIC_TAG_LEN);
  return true;
}

OPENSSL_EXPORT int EVP_AEAD_CTX_open(const EVP_AEAD_CTX *ctx, uint8_t *out, size_t *out_len, size_t max_out_len, const uint8_t *nonce, size_t nonce_len, const uint8_t *in, size_t in_len, const uint8_t *ad, size_t ad_len) {
  NS_ASSERT(in_len > IQUIC_TAG_LEN);
  *out_len = std::min(in_len - IQUIC_TAG_LEN, max_out_len);
  memcpy(out, in, *out_len);
  return true;
}

OPENSSL_EXPORT BIO *BIO_new_mem_buf(const void *buf, ossl_ssize_t len) { TODO; }

OPENSSL_EXPORT X509 *d2i_X509_bio(BIO *bp, X509 **x509) { TODO; }

OPENSSL_EXPORT int BIO_free(BIO *bio) { TODO; }

OPENSSL_EXPORT void EVP_MD_CTX_init(EVP_MD_CTX *ctx) { TODO; }

OPENSSL_EXPORT int EVP_DigestSignInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey) { TODO; }

OPENSSL_EXPORT int EVP_PKEY_CTX_set_rsa_padding(EVP_PKEY_CTX *ctx, int padding) { TODO; }

OPENSSL_EXPORT int EVP_PKEY_CTX_set_rsa_pss_saltlen(EVP_PKEY_CTX *ctx, int salt_len) { TODO; }

OPENSSL_EXPORT int EVP_DigestSignUpdate(EVP_MD_CTX *ctx, const void *data, size_t len) { TODO; }

OPENSSL_EXPORT int EVP_DigestSignFinal(EVP_MD_CTX *ctx, uint8_t *out_sig, size_t *out_sig_len) { TODO; }

OPENSSL_EXPORT int EVP_MD_CTX_cleanup(EVP_MD_CTX *ctx) { TODO; }

OPENSSL_EXPORT int EVP_DigestVerifyInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey) { TODO; }

OPENSSL_EXPORT int EVP_DigestVerifyUpdate(EVP_MD_CTX *ctx, const void *data, size_t len) { TODO; }

OPENSSL_EXPORT int EVP_DigestVerifyFinal(EVP_MD_CTX *ctx, const uint8_t *sig, size_t sig_len) { TODO; }

OPENSSL_EXPORT void CRYPTO_library_init(void) {
}

OPENSSL_EXPORT size_t EVP_AEAD_key_length(const EVP_AEAD *aead) {
  return CONST(1);
}

OPENSSL_EXPORT size_t EVP_AEAD_nonce_length(const EVP_AEAD *aead) {
  return CONST(1);
}

OPENSSL_EXPORT int EVP_AEAD_CTX_init_with_direction( EVP_AEAD_CTX *ctx, const EVP_AEAD *aead, const uint8_t *key, size_t key_len, size_t tag_len, enum evp_aead_direction_t dir) {
  NS_ASSERT(tag_len == IQUIC_TAG_LEN);
  return true;
}

OPENSSL_EXPORT void EVP_AEAD_CTX_cleanup(EVP_AEAD_CTX *ctx) {
}

OPENSSL_EXPORT int EVP_EncryptUpdate(EVP_CIPHER_CTX *ctx, uint8_t *out, int *out_len, const uint8_t *in, int in_len) {
  *out_len = in_len;
  memcpy(out, in, in_len);
  return true;
}

OPENSSL_EXPORT uint32_t ERR_get_error(void) { TODO; }

OPENSSL_EXPORT void CRYPTO_chacha_20(uint8_t *out, const uint8_t *in, size_t in_len, const uint8_t key[32], const uint8_t nonce[12], uint32_t counter) { TODO; }

OPENSSL_EXPORT SSL_SESSION *SSL_SESSION_from_bytes(const uint8_t *in, size_t in_len, const SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT int SSL_SESSION_early_data_capable(const SSL_SESSION *session) { TODO; }

OPENSSL_EXPORT void SSL_SESSION_free(SSL_SESSION *session) { TODO; }

OPENSSL_EXPORT SSL_CTX *SSL_CTX_new(const SSL_METHOD *method) {
  return LINE_STUB;
}

OPENSSL_EXPORT const SSL_METHOD *TLS_method(void) {
  return LINE_STUB;
}

OPENSSL_EXPORT char *ERR_error_string(uint32_t packed_error, char *buf) { TODO; }

OPENSSL_EXPORT int SSL_CTX_set_min_proto_version(SSL_CTX *ctx, uint16_t version) { TODO; }

OPENSSL_EXPORT int SSL_CTX_set_max_proto_version(SSL_CTX *ctx, uint16_t version) { TODO; }

OPENSSL_EXPORT int SSL_CTX_set_default_verify_paths(SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT int SSL_CTX_set_session_cache_mode(SSL_CTX *ctx, int mode) { TODO; }

OPENSSL_EXPORT void SSL_CTX_sess_set_new_cb( SSL_CTX *ctx, int (*new_session_cb)(SSL *ssl, SSL_SESSION *session)) { TODO; }

OPENSSL_EXPORT void SSL_CTX_set_custom_verify( SSL_CTX *ctx, int mode, enum ssl_verify_result_t (*callback)(SSL *ssl, uint8_t *out_alert)) { TODO; }

OPENSSL_EXPORT void SSL_CTX_set_early_data_enabled(SSL_CTX *ctx, int enabled) { TODO; }

OPENSSL_EXPORT SSL *SSL_new(SSL_CTX *ctx) {
  return new SSL{};
}

OPENSSL_EXPORT int SSL_set_quic_transport_params(SSL *ssl, const uint8_t *params, size_t params_len) {
  NS_ASSERT(not ssl->transport_params.has_value());
  if (ssl->state == SSL::INIT) {
    ssl->state = SSL::CLIENT_CONNECT;
  } else {
    NS_ASSERT(ssl->client.has_value() and not ssl->client.value());
    NS_ASSERT(ssl->state == SSL::SERVER_SET_TP);
    ssl->state = SSL::SERVER_WRITE_L2;
  }
  ssl->transport_params.emplace(params, params + params_len);
  return true;
}

OPENSSL_EXPORT int SSL_set_quic_method(SSL *ssl, const SSL_QUIC_METHOD *quic_method) {
  NS_ASSERT(ssl->io == nullptr);
  ssl->io = quic_method;
  return true;
}

OPENSSL_EXPORT int SSL_set_alpn_protos(SSL *ssl, const uint8_t *protos, size_t protos_len) { TODO; }

OPENSSL_EXPORT int SSL_set_tlsext_host_name(SSL *ssl, const char *name) {
  return true;
}

OPENSSL_EXPORT int SSL_set_session(SSL *ssl, SSL_SESSION *session) { TODO; }

OPENSSL_EXPORT int SSL_set_ex_data(SSL *ssl, int idx, void *data) {
  NS_ASSERT(not ssl->ex_data.has_value());
  ssl->ex_data.emplace(data);
  return true;
}

OPENSSL_EXPORT void SSL_set_connect_state(SSL *ssl) {
  NS_ASSERT(not ssl->client.has_value());
  NS_ASSERT(ssl->state == SSL::CLIENT_CONNECT);
  ssl->client.emplace(true);
  ssl->state = SSL::CLIENT_WRITE_L0;
}

OPENSSL_EXPORT int (*SSL_CTX_sess_get_new_cb(SSL_CTX *ctx))( SSL *ssl, SSL_SESSION *session) {
  return nullptr;
}

OPENSSL_EXPORT void SSL_CTX_free(SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT const EVP_AEAD *EVP_aead_aes_128_gcm(void) {
  return LINE_STUB;
}

OPENSSL_EXPORT const EVP_CIPHER *EVP_aes_128_ecb(void) {
  return LINE_STUB;
}

OPENSSL_EXPORT int HKDF_extract(uint8_t *out_key, size_t *out_len, const EVP_MD *digest, const uint8_t *secret, size_t secret_len, const uint8_t *salt, size_t salt_len) {
  *out_len = 32;
  memset(out_key, 0, *out_len);
  mix({out_key, *out_len}, {secret, secret_len});
  mix({out_key, *out_len}, {salt, salt_len});
  return true;
}

OPENSSL_EXPORT void EVP_CIPHER_CTX_init(EVP_CIPHER_CTX *ctx) {
}

OPENSSL_EXPORT int EVP_EncryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl, const uint8_t *key, const uint8_t *iv) {
  return true;
}

OPENSSL_EXPORT int EVP_CIPHER_CTX_cleanup(EVP_CIPHER_CTX *ctx) {
  return true;
}

OPENSSL_EXPORT void *SSL_get_ex_data(const SSL *ssl, int idx) {
  return ssl->ex_data.value();
}

OPENSSL_EXPORT STACK_OF(X509) *SSL_get_peer_cert_chain(const SSL *ssl) { TODO; }

OPENSSL_EXPORT const char *SSL_get_servername(const SSL *ssl, const int type) { TODO; }

OPENSSL_EXPORT SSL_CTX *SSL_set_SSL_CTX(SSL *ssl, SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT void SSL_set_verify(SSL *ssl, int mode, int (*callback)(int ok, X509_STORE_CTX *store_ctx)) { TODO; }

OPENSSL_EXPORT int SSL_CTX_get_verify_mode(const SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT void SSL_set_verify_depth(SSL *ssl, int depth) { TODO; }

OPENSSL_EXPORT int SSL_CTX_get_verify_depth(const SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT uint32_t SSL_clear_options(SSL *ssl, uint32_t options) {
  return 0;
}

OPENSSL_EXPORT uint32_t SSL_get_options(const SSL *ssl) { TODO; }

OPENSSL_EXPORT uint32_t SSL_set_options(SSL *ssl, uint32_t options) { TODO; }

OPENSSL_EXPORT uint32_t SSL_CTX_get_options(const SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT int SSL_set_quic_early_data_context(SSL *ssl, const uint8_t *context, size_t context_len) {
  return true;
}

OPENSSL_EXPORT void SSL_set_cert_cb(SSL *ssl, int (*cb)(SSL *ssl, void *arg), void *arg) { TODO; }

OPENSSL_EXPORT void SSL_set_accept_state(SSL *ssl) {
  NS_ASSERT(not ssl->client.has_value());
  NS_ASSERT(ssl->state == SSL::INIT);
  ssl->client.emplace(false);
  ssl->state = SSL::SERVER_READ_L0;
}

OPENSSL_EXPORT void SSL_get_peer_quic_transport_params( const SSL *ssl, const uint8_t **out_params, size_t *out_params_len) {
  NS_ASSERT(ssl->peer_transport_params.has_value());
  auto &peer_transport_params = ssl->peer_transport_params.value();
  *out_params = peer_transport_params.data();
  *out_params_len = peer_transport_params.size();
}

OPENSSL_EXPORT int SSL_SESSION_to_bytes(const SSL_SESSION *in, uint8_t **out_data, size_t *out_len) { TODO; }

OPENSSL_EXPORT void OPENSSL_free(void *ptr) { TODO; }

OPENSSL_EXPORT uint32_t SSL_CIPHER_get_id(const SSL_CIPHER *cipher) {
  return 0x03000000 | 0x1301;
}

OPENSSL_EXPORT const EVP_MD *EVP_sha384(void) { TODO; }

OPENSSL_EXPORT const EVP_AEAD *EVP_aead_aes_256_gcm(void) { TODO; }

OPENSSL_EXPORT const EVP_CIPHER *EVP_aes_256_ecb(void) { TODO; }

OPENSSL_EXPORT const EVP_AEAD *EVP_aead_chacha20_poly1305(void) {
  return LINE_STUB;
}

OPENSSL_EXPORT int SSL_do_handshake(SSL *ssl) {
  auto _write = [&](ssl_encryption_level_t level, std::vector<uint8_t> s) {
    NS_ASSERT(ssl->io->add_handshake_data(ssl, level, s.data(), s.size()) == 1);
  };
  auto write = [&](ssl_encryption_level_t level, std::string_view s) {
    _write(level, {s.data(), s.data() + s.size()});
  };
  auto write_tp = [&](ssl_encryption_level_t level) {
    NS_ASSERT(ssl->transport_params.has_value());
    auto s = ssl->transport_params.value();
    if (ssl->client.value()) {
      NS_ASSERT(s.size() <= 255);
      uint8_t chlo[4]{1, 0, 0, (uint8_t)s.size()};
      s.insert_range(s.begin(), chlo);
    }
    _write(level, s);
  };
  NS_ASSERT(ssl->client.has_value());
  switch (ssl->state) {
    case SSL::SERVER_READ_L0:
    case SSL::CLIENT_READ_L0:
    case SSL::CLIENT_READ_L2:
    case SSL::SERVER_READ_L2:
      return -1;

    case SSL::CLIENT_SET_WL2_RL2: {
      ssl->setWriteSecret(ssl_encryption_handshake);
      ssl->setReadSecret(ssl_encryption_handshake);
      ssl->state = SSL::CLIENT_READ_L2;
      return -1;
    }

    case SSL::CLIENT_WRITE_L0: {
      write_tp(ssl_encryption_initial);
      ssl->state = SSL::CLIENT_READ_L0;
      break;
    }
    case SSL::SERVER_WRITE_L0: {
      ssl->state = SSL::SERVER_SET_TP;
      write(ssl_encryption_initial, "SERVER_WRITE_L0");
      NS_ASSERT(ssl->state == SSL::SERVER_WRITE_L2);
      ssl->setWriteSecret(ssl_encryption_handshake);
      write_tp(ssl_encryption_handshake);
      ssl->setWriteSecret(ssl_encryption_application);
      ssl->state = SSL::SERVER_READ_L2;
      NS_ASSERT(ssl->io->flush_flight(ssl) == 1);
      ssl->setReadSecret(ssl_encryption_handshake);
      return -1;
    }
    case SSL::SERVER_WRITE_L2:
      abort();
    case SSL::CLIENT_WRITE_L2: {
      write(ssl_encryption_handshake, "CLIENT_WRITE_L2");
      ssl->setWriteSecret(ssl_encryption_application);
      ssl->setReadSecret(ssl_encryption_application);
      ssl->state = SSL::ESTABLISHED;
      break;
    }
    case SSL::SERVER_WRITE_L3: {
      ssl->setReadSecret(ssl_encryption_application);
      write(ssl_encryption_application, "SERVER_WRITE_L3");
      ssl->state = SSL::ESTABLISHED;
      break;
    }

    default:
      abort();
  }
  NS_ASSERT(ssl->io->flush_flight(ssl) == 1);
  return ssl->state == SSL::ESTABLISHED ? true : -1;
}

OPENSSL_EXPORT int SSL_get_error(const SSL *ssl, int ret_code) {
  switch (ssl->state) {
    case SSL::CLIENT_WRITE_L0:
    case SSL::SERVER_WRITE_L0:
    case SSL::SERVER_WRITE_L2:
    case SSL::CLIENT_WRITE_L2:
    case SSL::SERVER_WRITE_L3:
      return SSL_ERROR_WANT_WRITE;
    case SSL::SERVER_READ_L0:
    case SSL::CLIENT_READ_L0:
    case SSL::CLIENT_READ_L2:
    case SSL::SERVER_READ_L2:
      return SSL_ERROR_WANT_READ;
    default:
      abort();
  }
}

OPENSSL_EXPORT void SSL_reset_early_data_reject(SSL *ssl) { TODO; }

OPENSSL_EXPORT int SSL_in_early_data(const SSL *ssl) {
  return false;
}

OPENSSL_EXPORT int SSL_process_quic_post_handshake(SSL *ssl) {
  return true;
}

OPENSSL_EXPORT void SSL_free(SSL *ssl) {
  delete ssl;
}

OPENSSL_EXPORT int SSL_get_ex_new_index(long argl, void *argp, CRYPTO_EX_unused *unused, CRYPTO_EX_dup *dup_unused, CRYPTO_EX_free *free_func) {
  return LINE_STUB;
}

OPENSSL_EXPORT STACK_OF(X509) *X509_chain_up_ref(STACK_OF(X509) *chain) { TODO; }

OPENSSL_EXPORT const SSL_CIPHER *SSL_get_current_cipher(const SSL *ssl) {
  return nullptr;
}

OPENSSL_EXPORT const char *SSL_CIPHER_get_name(const SSL_CIPHER *cipher) {
  NS_ASSERT(cipher == nullptr);
  return "TLS_AES_128_GCM_SHA256";
}

OPENSSL_EXPORT int SSL_in_init(const SSL *ssl) {
  return ssl->state != SSL::ESTABLISHED;
}

OPENSSL_EXPORT int SSL_provide_quic_data(SSL *ssl, enum ssl_encryption_level_t level, const uint8_t *data, size_t len) {
  auto set_tp = [&] {
    NS_ASSERT(not ssl->peer_transport_params.has_value());
    auto client = ssl->client.value();
    auto i = client ? 0 : 4;
    auto n = len - i;
    ssl->peer_transport_params.emplace(data + i, data + i + n);
  };
  switch (ssl->state) {
    case SSL::SERVER_READ_L0: {
      NS_ASSERT(level == ssl_encryption_initial);
      set_tp();
      ssl->state = SSL::SERVER_WRITE_L0;
      break;
    }
    case SSL::CLIENT_READ_L0: {
      NS_ASSERT(level == ssl_encryption_initial);
      ssl->state = SSL::CLIENT_SET_WL2_RL2;
      break;
    }
    case SSL::CLIENT_READ_L2: {
      NS_ASSERT(level == ssl_encryption_handshake);
      set_tp();
      ssl->state = SSL::CLIENT_WRITE_L2;
      break;
    }
    case SSL::SERVER_READ_L2: {
      ssl->state = SSL::SERVER_WRITE_L3;
      break;
    }
    default:
      abort();
  }
  return true;
}

OPENSSL_EXPORT int SSL_CIPHER_get_bits(const SSL_CIPHER *cipher, int *out_alg_bits) {
  NS_ASSERT(cipher == nullptr);
  *out_alg_bits = 128;
  return 128;
}

OPENSSL_EXPORT void SSL_get0_alpn_selected(const SSL *ssl, const uint8_t **out_data, unsigned *out_len) { TODO; }

OPENSSL_EXPORT int SSL_send_fatal_alert(SSL *ssl, uint8_t alert) { TODO; }

OPENSSL_EXPORT int EVP_AEAD_CTX_init(EVP_AEAD_CTX *ctx, const EVP_AEAD *aead, const uint8_t *key, size_t key_len, size_t tag_len, ENGINE *impl) {
  NS_ASSERT(tag_len == IQUIC_TAG_LEN);
  return true;
}

OPENSSL_EXPORT X509_NAME *X509_get_subject_name(const X509 *x509) { TODO; }

OPENSSL_EXPORT char *X509_NAME_oneline(const X509_NAME *name, char *buf, int size) { TODO; }

OPENSSL_EXPORT int SSL_CTX_get_ex_new_index(long argl, void *argp, CRYPTO_EX_unused *unused, CRYPTO_EX_dup *dup_unused, CRYPTO_EX_free *free_func) {
  return LINE_STUB;
}

OPENSSL_EXPORT int SSL_CTX_get0_chain_certs(const SSL_CTX *ctx, STACK_OF(X509) **out_chain) { TODO; }

OPENSSL_EXPORT int i2d_X509(X509 *x509, uint8_t **outp) { TODO; }

OPENSSL_EXPORT int SSL_CTX_set_ex_data(SSL_CTX *ctx, int idx, void *data) { TODO; }

OPENSSL_EXPORT EVP_PKEY *SSL_CTX_get0_privatekey(const SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT int EVP_PKEY_size(const EVP_PKEY *pkey) { TODO; }

OPENSSL_EXPORT void *SSL_CTX_get_ex_data(const SSL_CTX *ctx, int idx) { TODO; }

OPENSSL_EXPORT X509_NAME *X509_get_issuer_name(const X509 *x509) { TODO; }

OPENSSL_EXPORT int X509_NAME_get0_der(X509_NAME *name, const uint8_t **out_der, size_t *out_der_len) { TODO; }

OPENSSL_EXPORT ASN1_INTEGER *X509_get_serialNumber(X509 *x509) { TODO; }

OPENSSL_EXPORT BIGNUM *ASN1_INTEGER_to_BN(const ASN1_INTEGER *ai, BIGNUM *bn) { TODO; }

OPENSSL_EXPORT unsigned BN_num_bytes(const BIGNUM *bn) { TODO; }

OPENSSL_EXPORT void BN_free(BIGNUM *bn) { TODO; }

OPENSSL_EXPORT size_t BN_bn2bin(const BIGNUM *in, uint8_t *out) { TODO; }

OPENSSL_EXPORT X509 *SSL_CTX_get0_certificate(const SSL_CTX *ctx) { TODO; }

OPENSSL_EXPORT EVP_PKEY *X509_get_pubkey(const X509 *x509) { TODO; }

OPENSSL_EXPORT void EVP_PKEY_free(EVP_PKEY *pkey) { TODO; }

OPENSSL_EXPORT void X509_free(X509 *x509) { TODO; }

OPENSSL_EXPORT X509 *PEM_read_bio_X509(BIO *bp, X509 **x, pem_password_cb *cb, void *u) { TODO; }

OPENSSL_EXPORT size_t sk_X509_num(const STACK_OF_X509 *sk) { TODO; }

OPENSSL_EXPORT X509 *sk_X509_value(const STACK_OF_X509 *sk, size_t i) { TODO; }

OPENSSL_EXPORT STACK_OF_X509 *sk_X509_new_null(void) { TODO; }

OPENSSL_EXPORT size_t sk_X509_push(STACK_OF_X509 *sk, X509 *p) { TODO; }

OPENSSL_EXPORT void sk_X509_free(STACK_OF_X509 *sk) { TODO; }
