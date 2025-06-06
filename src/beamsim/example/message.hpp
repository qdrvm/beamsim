#pragma once

#include <beamsim/message.hpp>
#include <beamsim/peer_index.hpp>
#include <bit>
#include <variant>
#include <vector>

namespace beamsim::example {
  constexpr size_t kSizeSignature = 1536;
  constexpr size_t kSizeSnark = 131072;

  struct BitSet {
    size_t byteSize() const {
      return limbs_.size() * sizeof(Limb);
    }
    void hash(MessageHasher &hasher) const {
      hasher.hash(limbs_);
    }

    void set(PeerIndex i) {
      size_t i1 = i / limb_bits, i2 = i % limb_bits;
      if (limbs_.size() <= i1) {
        limbs_.resize(i1 + 1);
      }
      limbs_[i1] |= Limb{1} << i2;
    }
    void set(const BitSet &other) {
      if (limbs_.size() < other.limbs_.size()) {
        limbs_.resize(other.limbs_.size());
      }
      for (size_t i = 0; i < limbs_.size(); ++i) {
        limbs_.at(i) |= other.limbs_.at(i);
      }
    }

    PeerIndex ones() const {
      PeerIndex n = 0;
      for (auto &limb : limbs_) {
        n += std::popcount(limb);
      }
      return n;
    }

    using Limb = uint8_t;
    static constexpr size_t limb_bits = 8 * sizeof(Limb);
    std::vector<Limb> limbs_;
  };

  struct MessageSignature {
    PeerIndex peer_index;
  };
  struct MessageSnark1 {
    BitSet peer_indices;
  };
  struct MessageSnark2 {
    BitSet peer_indices;
  };

  class Message : public IMessage {
   public:
    using Variant =
        std::variant<MessageSignature, MessageSnark1, MessageSnark2>;
    Message(Variant variant) : variant{std::move(variant)} {}

    // IMessage
    MessageSize size() const override {
      if (auto *signature = std::get_if<MessageSignature>(&variant)) {
        return sizeof(signature->peer_index) + kSizeSignature;
      } else if (auto *snark1 = std::get_if<MessageSnark1>(&variant)) {
        return snark1->peer_indices.byteSize() + kSizeSnark;
      } else {
        auto &snark2 = std::get<MessageSnark2>(variant);
        return snark2.peer_indices.byteSize() + kSizeSnark;
      }
    }
    void hash(MessageHasher &hasher) const override {
      if (auto *signature = std::get_if<MessageSignature>(&variant)) {
        hasher.hash(signature->peer_index);
      } else if (auto *snark1 = std::get_if<MessageSnark1>(&variant)) {
        snark1->peer_indices.hash(hasher);
      } else {
        auto &snark2 = std::get<MessageSnark2>(variant);
        snark2.peer_indices.hash(hasher);
      }
    }

    Variant variant;
  };
}  // namespace beamsim::example
