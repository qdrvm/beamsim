#pragma once

#include <beamsim/consts.hpp>
#include <beamsim/message.hpp>
#include <beamsim/peer_index.hpp>
#include <bit>
#include <variant>
#include <vector>

namespace beamsim::example {
  struct BitSet {
    friend void encodeTo(MessageEncodeTo &to, const BitSet &v) {
      encodeTo(to, v.limbs_);
    }
    friend void decodeFrom(MessageDecodeFrom &from, BitSet &v) {
      decodeFrom(from, v.limbs_);
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
      for (size_t i = 0; i < limbs_.size() and i < other.limbs_.size(); ++i) {
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
    MessageSize padding() const override {
      if (std::holds_alternative<MessageSignature>(variant)) {
        return consts().signature_size;
      } else if (std::holds_alternative<MessageSnark1>(variant)) {
        return consts().snark_size;
      } else {
        std::get<MessageSnark2>(variant);
        return consts().snark_size;
      }
    }
    void encode(MessageEncodeTo &to) const override {
      encodeTo(to, (uint8_t)variant.index());
      if (auto *signature = std::get_if<MessageSignature>(&variant)) {
        encodeTo(to, signature->peer_index);
      } else if (auto *snark1 = std::get_if<MessageSnark1>(&variant)) {
        encodeTo(to, snark1->peer_indices);
      } else {
        auto &snark2 = std::get<MessageSnark2>(variant);
        encodeTo(to, snark2.peer_indices);
      }
    }
    static MessagePtr decode(MessageDecodeFrom &from) {
      auto i = from.get<uint8_t>();
      if (i == 0) {
        MessageSignature signature;
        decodeFrom(from, signature.peer_index);
        return std::make_shared<Message>(std::move(signature));
      } else if (i == 1) {
        MessageSnark1 snark1;
        decodeFrom(from, snark1.peer_indices);
        return std::make_shared<Message>(std::move(snark1));
      } else {
        MessageSnark2 snark2;
        decodeFrom(from, snark2.peer_indices);
        return std::make_shared<Message>(std::move(snark2));
      }
    }

    Variant variant;
  };
}  // namespace beamsim::example
