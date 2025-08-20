#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(__cplusplus)
#define OPENSSL_EXPORT extern "C"
#else
#define OPENSSL_EXPORT
#endif

#define STACK_OF(x) STACK_OF_##x

#define LN_aes_128_gcm "aes-128-gcm"
#define SHA256_DIGEST_LENGTH 32
#define EVP_MAX_BLOCK_LENGTH 32
#define TLSEXT_NAMETYPE_host_name 0
#define SSL_OP_NO_TLSv1_3 0x20000000L
#define SSL_ERROR_EARLY_DATA_REJECTED 15
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define EVP_MAX_KEY_LENGTH 64
#define ERR_ERROR_STRING_BUF_LEN 120
#define TLS1_3_VERSION 0x0304
#define SSL_SESS_CACHE_CLIENT 0x0001
#define SSL_VERIFY_PEER 0x01
#define EVP_MAX_MD_SIZE 64
#define EVP_MAX_IV_LENGTH 16
#define RSA_PKCS1_PSS_PADDING 6
#define RSA_PKCS1_PSS_PADDING 6

enum evp_aead_direction_t {
  evp_aead_open,
  evp_aead_seal,
};
enum ssl_verify_result_t {
  ssl_verify_ok,
  ssl_verify_invalid,
  ssl_verify_retry,
};
enum ssl_encryption_level_t {
  ssl_encryption_initial = 0,
  ssl_encryption_early_data = 1,
  ssl_encryption_handshake = 2,
  ssl_encryption_application = 3,
};

typedef struct x509_store_ctx_st X509_STORE_CTX;
typedef struct sha256_state_st SHA256_CTX;
typedef struct env_md_st EVP_MD;
typedef struct bignum_st BIGNUM;
typedef struct evp_aead_ctx_st EVP_AEAD_CTX;
typedef struct ssl_method_st SSL_METHOD;
typedef struct x509_st X509;
typedef struct bio_st BIO;
typedef ptrdiff_t ossl_ssize_t;
typedef struct asn1_string_st ASN1_INTEGER;
typedef struct x509_st X509;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct env_md_ctx_st EVP_MD_CTX;
typedef struct evp_pkey_ctx_st EVP_PKEY_CTX;
typedef struct engine_st ENGINE;
typedef struct X509_name_st X509_NAME;
typedef struct ssl_quic_method_st SSL_QUIC_METHOD;
typedef struct crypto_ex_data_st CRYPTO_EX_DATA;
typedef struct ssl_st SSL;
typedef struct ssl_session_st SSL_SESSION;
typedef struct ssl_cipher_st SSL_CIPHER;
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
typedef struct evp_cipher_st EVP_CIPHER;
typedef struct stack_st_X509 STACK_OF_X509;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct evp_aead_st EVP_AEAD;

typedef int pem_password_cb(char *buf, int size, int rwflag, void *userdata);

typedef int CRYPTO_EX_unused;
typedef int CRYPTO_EX_dup(CRYPTO_EX_DATA *to, const CRYPTO_EX_DATA *from, void **from_d, int index, long argl, void *argp);
typedef void CRYPTO_EX_free(void *parent, void *ptr, CRYPTO_EX_DATA *ad, int index, long argl, void *argp);

OPENSSL_EXPORT int RAND_bytes(uint8_t *buf, size_t len);
OPENSSL_EXPORT int SHA256_Init(SHA256_CTX *sha);
OPENSSL_EXPORT int SHA256_Update(SHA256_CTX *sha, const void *data, size_t len);
OPENSSL_EXPORT int SHA256_Final(uint8_t out[SHA256_DIGEST_LENGTH], SHA256_CTX *sha);
OPENSSL_EXPORT uint8_t *HMAC(const EVP_MD *evp_md, const void *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out, unsigned int *out_len);
OPENSSL_EXPORT const EVP_MD *EVP_sha256(void);
OPENSSL_EXPORT int HKDF_expand(uint8_t *out_key, size_t out_len, const EVP_MD *digest, const uint8_t *prk, size_t prk_len, const uint8_t *info, size_t info_len);
OPENSSL_EXPORT void X25519_public_from_private(uint8_t out_public_value[32], const uint8_t private_key[32]);
OPENSSL_EXPORT int X25519(uint8_t out_shared_key[32], const uint8_t private_key[32], const uint8_t peer_public_value[32]);
OPENSSL_EXPORT int EVP_AEAD_CTX_seal(const EVP_AEAD_CTX *ctx, uint8_t *out, size_t *out_len, size_t max_out_len, const uint8_t *nonce, size_t nonce_len, const uint8_t *in, size_t in_len, const uint8_t *ad, size_t ad_len);
OPENSSL_EXPORT int EVP_AEAD_CTX_open(const EVP_AEAD_CTX *ctx, uint8_t *out, size_t *out_len, size_t max_out_len, const uint8_t *nonce, size_t nonce_len, const uint8_t *in, size_t in_len, const uint8_t *ad, size_t ad_len);
OPENSSL_EXPORT BIO *BIO_new_mem_buf(const void *buf, ossl_ssize_t len);
OPENSSL_EXPORT X509 *d2i_X509_bio(BIO *bp, X509 **x509);
OPENSSL_EXPORT int BIO_free(BIO *bio);
OPENSSL_EXPORT void EVP_MD_CTX_init(EVP_MD_CTX *ctx);
OPENSSL_EXPORT int EVP_DigestSignInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);
OPENSSL_EXPORT int EVP_PKEY_CTX_set_rsa_padding(EVP_PKEY_CTX *ctx, int padding);
OPENSSL_EXPORT int EVP_PKEY_CTX_set_rsa_pss_saltlen(EVP_PKEY_CTX *ctx, int salt_len);
OPENSSL_EXPORT int EVP_DigestSignUpdate(EVP_MD_CTX *ctx, const void *data, size_t len);
OPENSSL_EXPORT int EVP_DigestSignFinal(EVP_MD_CTX *ctx, uint8_t *out_sig, size_t *out_sig_len);
OPENSSL_EXPORT int EVP_MD_CTX_cleanup(EVP_MD_CTX *ctx);
OPENSSL_EXPORT int EVP_DigestVerifyInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);
OPENSSL_EXPORT int EVP_DigestVerifyUpdate(EVP_MD_CTX *ctx, const void *data, size_t len);
OPENSSL_EXPORT int EVP_DigestVerifyFinal(EVP_MD_CTX *ctx, const uint8_t *sig, size_t sig_len);
OPENSSL_EXPORT void CRYPTO_library_init(void);
OPENSSL_EXPORT size_t EVP_AEAD_key_length(const EVP_AEAD *aead);
OPENSSL_EXPORT size_t EVP_AEAD_nonce_length(const EVP_AEAD *aead);
OPENSSL_EXPORT int EVP_AEAD_CTX_init_with_direction( EVP_AEAD_CTX *ctx, const EVP_AEAD *aead, const uint8_t *key, size_t key_len, size_t tag_len, enum evp_aead_direction_t dir);
OPENSSL_EXPORT void EVP_AEAD_CTX_cleanup(EVP_AEAD_CTX *ctx);
OPENSSL_EXPORT int EVP_EncryptUpdate(EVP_CIPHER_CTX *ctx, uint8_t *out, int *out_len, const uint8_t *in, int in_len);
OPENSSL_EXPORT uint32_t ERR_get_error(void);
OPENSSL_EXPORT void CRYPTO_chacha_20(uint8_t *out, const uint8_t *in, size_t in_len, const uint8_t key[32], const uint8_t nonce[12], uint32_t counter);
OPENSSL_EXPORT SSL_SESSION *SSL_SESSION_from_bytes(const uint8_t *in, size_t in_len, const SSL_CTX *ctx);
OPENSSL_EXPORT int SSL_SESSION_early_data_capable(const SSL_SESSION *session);
OPENSSL_EXPORT void SSL_SESSION_free(SSL_SESSION *session);
OPENSSL_EXPORT SSL_CTX *SSL_CTX_new(const SSL_METHOD *method);
OPENSSL_EXPORT const SSL_METHOD *TLS_method(void);
OPENSSL_EXPORT char *ERR_error_string(uint32_t packed_error, char *buf);
OPENSSL_EXPORT int SSL_CTX_set_min_proto_version(SSL_CTX *ctx, uint16_t version);
OPENSSL_EXPORT int SSL_CTX_set_max_proto_version(SSL_CTX *ctx, uint16_t version);
OPENSSL_EXPORT int SSL_CTX_set_default_verify_paths(SSL_CTX *ctx);
OPENSSL_EXPORT int SSL_CTX_set_session_cache_mode(SSL_CTX *ctx, int mode);
OPENSSL_EXPORT void SSL_CTX_sess_set_new_cb( SSL_CTX *ctx, int (*new_session_cb)(SSL *ssl, SSL_SESSION *session));
OPENSSL_EXPORT void SSL_CTX_set_custom_verify( SSL_CTX *ctx, int mode, enum ssl_verify_result_t (*callback)(SSL *ssl, uint8_t *out_alert));
OPENSSL_EXPORT void SSL_CTX_set_early_data_enabled(SSL_CTX *ctx, int enabled);
OPENSSL_EXPORT SSL *SSL_new(SSL_CTX *ctx);
OPENSSL_EXPORT int SSL_set_quic_transport_params(SSL *ssl, const uint8_t *params, size_t params_len);
OPENSSL_EXPORT int SSL_set_quic_method(SSL *ssl, const SSL_QUIC_METHOD *quic_method);
OPENSSL_EXPORT int SSL_set_alpn_protos(SSL *ssl, const uint8_t *protos, size_t protos_len);
OPENSSL_EXPORT int SSL_set_tlsext_host_name(SSL *ssl, const char *name);
OPENSSL_EXPORT int SSL_set_session(SSL *ssl, SSL_SESSION *session);
OPENSSL_EXPORT int SSL_set_ex_data(SSL *ssl, int idx, void *data);
OPENSSL_EXPORT void SSL_set_connect_state(SSL *ssl);
OPENSSL_EXPORT int (*SSL_CTX_sess_get_new_cb(SSL_CTX *ctx))( SSL *ssl, SSL_SESSION *session);
OPENSSL_EXPORT void SSL_CTX_free(SSL_CTX *ctx);
OPENSSL_EXPORT const EVP_AEAD *EVP_aead_aes_128_gcm(void);
OPENSSL_EXPORT const EVP_CIPHER *EVP_aes_128_ecb(void);
OPENSSL_EXPORT int HKDF_extract(uint8_t *out_key, size_t *out_len, const EVP_MD *digest, const uint8_t *secret, size_t secret_len, const uint8_t *salt, size_t salt_len);
OPENSSL_EXPORT void EVP_CIPHER_CTX_init(EVP_CIPHER_CTX *ctx);
OPENSSL_EXPORT int EVP_EncryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl, const uint8_t *key, const uint8_t *iv);
OPENSSL_EXPORT int EVP_CIPHER_CTX_cleanup(EVP_CIPHER_CTX *ctx);
OPENSSL_EXPORT void *SSL_get_ex_data(const SSL *ssl, int idx);
OPENSSL_EXPORT STACK_OF(X509) *SSL_get_peer_cert_chain(const SSL *ssl);
OPENSSL_EXPORT const char *SSL_get_servername(const SSL *ssl, const int type);
OPENSSL_EXPORT SSL_CTX *SSL_set_SSL_CTX(SSL *ssl, SSL_CTX *ctx);
OPENSSL_EXPORT void SSL_set_verify(SSL *ssl, int mode, int (*callback)(int ok, X509_STORE_CTX *store_ctx));
OPENSSL_EXPORT int SSL_CTX_get_verify_mode(const SSL_CTX *ctx);
OPENSSL_EXPORT void SSL_set_verify_depth(SSL *ssl, int depth);
OPENSSL_EXPORT int SSL_CTX_get_verify_depth(const SSL_CTX *ctx);
OPENSSL_EXPORT uint32_t SSL_clear_options(SSL *ssl, uint32_t options);
OPENSSL_EXPORT uint32_t SSL_get_options(const SSL *ssl);
OPENSSL_EXPORT uint32_t SSL_set_options(SSL *ssl, uint32_t options);
OPENSSL_EXPORT uint32_t SSL_CTX_get_options(const SSL_CTX *ctx);
OPENSSL_EXPORT int SSL_set_quic_early_data_context(SSL *ssl, const uint8_t *context, size_t context_len);
OPENSSL_EXPORT void SSL_set_cert_cb(SSL *ssl, int (*cb)(SSL *ssl, void *arg), void *arg);
OPENSSL_EXPORT void SSL_set_accept_state(SSL *ssl);
OPENSSL_EXPORT void SSL_get_peer_quic_transport_params( const SSL *ssl, const uint8_t **out_params, size_t *out_params_len);
OPENSSL_EXPORT int SSL_SESSION_to_bytes(const SSL_SESSION *in, uint8_t **out_data, size_t *out_len);
OPENSSL_EXPORT void OPENSSL_free(void *ptr);
OPENSSL_EXPORT uint32_t SSL_CIPHER_get_id(const SSL_CIPHER *cipher);
OPENSSL_EXPORT const EVP_MD *EVP_sha384(void);
OPENSSL_EXPORT const EVP_AEAD *EVP_aead_aes_256_gcm(void);
OPENSSL_EXPORT const EVP_CIPHER *EVP_aes_256_ecb(void);
OPENSSL_EXPORT const EVP_AEAD *EVP_aead_chacha20_poly1305(void);
OPENSSL_EXPORT int SSL_do_handshake(SSL *ssl);
OPENSSL_EXPORT int SSL_get_error(const SSL *ssl, int ret_code);
OPENSSL_EXPORT void SSL_reset_early_data_reject(SSL *ssl);
OPENSSL_EXPORT int SSL_in_early_data(const SSL *ssl);
OPENSSL_EXPORT int SSL_process_quic_post_handshake(SSL *ssl);
OPENSSL_EXPORT void SSL_free(SSL *ssl);
OPENSSL_EXPORT int SSL_get_ex_new_index(long argl, void *argp, CRYPTO_EX_unused *unused, CRYPTO_EX_dup *dup_unused, CRYPTO_EX_free *free_func);
OPENSSL_EXPORT STACK_OF(X509) *X509_chain_up_ref(STACK_OF(X509) *chain);
OPENSSL_EXPORT const SSL_CIPHER *SSL_get_current_cipher(const SSL *ssl);
OPENSSL_EXPORT const char *SSL_CIPHER_get_name(const SSL_CIPHER *cipher);
OPENSSL_EXPORT int SSL_in_init(const SSL *ssl);
OPENSSL_EXPORT int SSL_provide_quic_data(SSL *ssl, enum ssl_encryption_level_t level, const uint8_t *data, size_t len);
OPENSSL_EXPORT int SSL_CIPHER_get_bits(const SSL_CIPHER *cipher, int *out_alg_bits);
OPENSSL_EXPORT void SSL_get0_alpn_selected(const SSL *ssl, const uint8_t **out_data, unsigned *out_len);
OPENSSL_EXPORT int SSL_send_fatal_alert(SSL *ssl, uint8_t alert);
OPENSSL_EXPORT int EVP_AEAD_CTX_init(EVP_AEAD_CTX *ctx, const EVP_AEAD *aead, const uint8_t *key, size_t key_len, size_t tag_len, ENGINE *impl);
OPENSSL_EXPORT X509_NAME *X509_get_subject_name(const X509 *x509);
OPENSSL_EXPORT char *X509_NAME_oneline(const X509_NAME *name, char *buf, int size);
OPENSSL_EXPORT int SSL_CTX_get_ex_new_index(long argl, void *argp, CRYPTO_EX_unused *unused, CRYPTO_EX_dup *dup_unused, CRYPTO_EX_free *free_func);
OPENSSL_EXPORT int SSL_CTX_get0_chain_certs(const SSL_CTX *ctx, STACK_OF(X509) **out_chain);
OPENSSL_EXPORT int i2d_X509(X509 *x509, uint8_t **outp);
OPENSSL_EXPORT int SSL_CTX_set_ex_data(SSL_CTX *ctx, int idx, void *data);
OPENSSL_EXPORT EVP_PKEY *SSL_CTX_get0_privatekey(const SSL_CTX *ctx);
OPENSSL_EXPORT int EVP_PKEY_size(const EVP_PKEY *pkey);
OPENSSL_EXPORT void *SSL_CTX_get_ex_data(const SSL_CTX *ctx, int idx);
OPENSSL_EXPORT X509_NAME *X509_get_issuer_name(const X509 *x509);
OPENSSL_EXPORT int X509_NAME_get0_der(X509_NAME *name, const uint8_t **out_der, size_t *out_der_len);
OPENSSL_EXPORT ASN1_INTEGER *X509_get_serialNumber(X509 *x509);
OPENSSL_EXPORT BIGNUM *ASN1_INTEGER_to_BN(const ASN1_INTEGER *ai, BIGNUM *bn);
OPENSSL_EXPORT unsigned BN_num_bytes(const BIGNUM *bn);
OPENSSL_EXPORT void BN_free(BIGNUM *bn);
OPENSSL_EXPORT size_t BN_bn2bin(const BIGNUM *in, uint8_t *out);
OPENSSL_EXPORT X509 *SSL_CTX_get0_certificate(const SSL_CTX *ctx);
OPENSSL_EXPORT EVP_PKEY *X509_get_pubkey(const X509 *x509);
OPENSSL_EXPORT void EVP_PKEY_free(EVP_PKEY *pkey);
OPENSSL_EXPORT void X509_free(X509 *x509);

OPENSSL_EXPORT X509 *PEM_read_bio_X509(BIO *bp, X509 **x, pem_password_cb *cb, void *u);
OPENSSL_EXPORT size_t sk_X509_num(const STACK_OF_X509 *sk);
OPENSSL_EXPORT X509 *sk_X509_value(const STACK_OF_X509 *sk, size_t i);
OPENSSL_EXPORT STACK_OF_X509 *sk_X509_new_null(void);
OPENSSL_EXPORT size_t sk_X509_push(STACK_OF_X509 *sk, X509 *p);
OPENSSL_EXPORT void sk_X509_free(STACK_OF_X509 *sk);

struct env_md_ctx_st {};
struct sha256_state_st {};
struct evp_aead_ctx_st {
  char _;
};
struct evp_cipher_ctx_st {};

struct ssl_quic_method_st {
  int (*set_read_secret)(SSL *ssl, enum ssl_encryption_level_t level, const SSL_CIPHER *cipher, const uint8_t *secret, size_t secret_len);
  int (*set_write_secret)(SSL *ssl, enum ssl_encryption_level_t level, const SSL_CIPHER *cipher, const uint8_t *secret, size_t secret_len);
  int (*add_handshake_data)(SSL *ssl, enum ssl_encryption_level_t level, const uint8_t *data, size_t len);
  int (*flush_flight)(SSL *ssl);
  int (*send_alert)(SSL *ssl, enum ssl_encryption_level_t level, uint8_t alert);
};
