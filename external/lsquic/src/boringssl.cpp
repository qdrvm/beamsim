#include <openssl/rand.h>

#include <random>
#include <span>

inline auto &randomEngine() {
  static std::default_random_engine random_engine;
  return random_engine;
}

extern "C" int RAND_bytes(uint8_t *buf, size_t len) {
  for (auto &byte : std::span{buf, len}) {
    byte = std::uniform_int_distribution<uint8_t>{}(randomEngine());
  }
  return 1;
}

extern "C" void CRYPTO_library_init() {}
