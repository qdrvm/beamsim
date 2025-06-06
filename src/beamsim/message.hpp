#pragma once

#include <memory>
#include <xxhash.hpp>

namespace beamsim {
  using MessageSize = uint32_t;
  using MessageHash = uint64_t;

  class MessageHasher {
   public:
    template <typename T>
      requires std::is_pod_v<T>
    void hash(const T &v) {
      state_.update(&v, sizeof(v));
    }
    template <typename T>
      requires std::is_pod_v<T>
    void hash(const std::vector<T> &v) {
      state_.update(v.data(), v.size() * sizeof(T));
    }
    MessageHash hash() const {
      return state_.digest();
    }

   private:
    xxh::hash_state64_t state_;
  };

  class IMessage {
   public:
    virtual ~IMessage() = default;

    // payload and padding
    virtual MessageSize size() const = 0;

    // used by gossip
    virtual void hash(MessageHasher &hasher) const = 0;
  };
  using MessagePtr = std::shared_ptr<IMessage>;

  MessageHash hashMessage(const IMessage &message) {
    MessageHasher hasher;
    message.hash(hasher);
    return hasher.hash();
  }
}  // namespace beamsim
