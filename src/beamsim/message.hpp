#pragma once

#include <xxhash.h>

#include <functional>
#include <memory>
#include <span>
#include <type_traits>

namespace beamsim {
  class IMessage;
  struct MessageDecodeFrom;

  using MessagePtr = std::shared_ptr<IMessage>;
  using MessageSize = uint32_t;
  using MessageHash = uint64_t;
  using Bytes = std::vector<uint8_t>;
  using BytesIn = std::span<const uint8_t>;
  template <size_t N>
  using BytesN = std::array<uint8_t, N>;
  using MessageDecodeFn = std::function<MessagePtr(MessageDecodeFrom &)>;
  using MessageForwardFn = std::function<void()>;

  struct MessageEncodeTo {
    std::function<void(BytesIn)> f;
  };
  struct MessageDecodeFrom {
    BytesIn data;

    template <typename T>
    T get() {
      T v;
      decodeFrom(*this, v);
      return v;
    }
  };

  void append(Bytes &l, auto &&r) {
    l.insert(l.end(), r.begin(), r.end());
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

  template <typename T>
    requires std::is_pod_v<T>
  void encodeTo(MessageEncodeTo &to, const T &v) {
    to.f({(const uint8_t *)&v, sizeof(T)});
  }
  template <typename T>
  void encodeTo(MessageEncodeTo &to, const std::vector<T> &v) {
    encodeTo(to, MessageSize(v.size()));
    if constexpr (std::is_pod_v<T>) {
      to.f({(const uint8_t *)v.data(), v.size() * sizeof(T)});
    } else {
      for (auto &x : v) {
        encodeTo(to, x);
      }
    }
  }

  template <typename T>
    requires std::is_pod_v<T>
  void decodeFrom(MessageDecodeFrom &from, T &v) {
    auto part = from.data.first(sizeof(T));
    memcpy(&v, part.data(), sizeof(T));
    from.data = from.data.subspan(sizeof(T));
  }
  template <typename T>
  void decodeFrom(MessageDecodeFrom &from, std::vector<T> &v) {
    v.resize(from.get<MessageSize>());
    for (auto &x : v) {
      decodeFrom(from, x);
    }
  }

  class MessageHasher {
   public:
    MessageHasher() {
      state = XXH3_createState();
      if (state == nullptr) {
        throw std::logic_error{"XXH3_createState"};
      }
      XXH3_64bits_reset(state);
    }

    ~MessageHasher() {
      XXH3_freeState(state);
    }

    void update(BytesIn part) {
      if (part.empty()) {
        return;
      }
      XXH3_64bits_update(state, part.data(), part.size());
    }

    MessageHash hash() const {
      return XXH3_64bits_digest(state);
    }

   private:
    XXH3_state_t *state;
  };

  class IMessage {
   public:
    virtual ~IMessage() = default;

    virtual MessageSize padding() const = 0;
    virtual void encode(MessageEncodeTo &to) const = 0;

    MessageSize dataSize() const {
      size_t size = 0;
      MessageEncodeTo to{[&size](BytesIn part) { size += part.size(); }};
      encode(to);
      return size;
    }

    Bytes encode() const {
      Bytes out;
      MessageEncodeTo to{[&out](BytesIn part) { append(out, part); }};
      encode(to);
      return out;
    }

    MessageHash hash() const {
      MessageHasher hasher;
      MessageEncodeTo to{[&hasher](BytesIn part) { hasher.update(part); }};
      encode(to);
      return hasher.hash();
    }
  };
}  // namespace beamsim
