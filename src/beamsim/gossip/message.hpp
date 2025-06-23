#pragma once

#include <beamsim/gossip/topic_index.hpp>
#include <beamsim/message.hpp>
#include <beamsim/peer_index.hpp>

namespace beamsim::gossip {
  // hack
  MessageDecodeFn message_decode;

  struct Publish {
    friend void encodeTo(MessageEncodeTo &to, const Publish &v) {
      encodeTo(to, v.topic_index);
      encodeTo(to, v.origin);
      v.message->encode(to);
    }
    friend void decodeFrom(MessageDecodeFrom &from, Publish &v) {
      decodeFrom(from, v.topic_index);
      decodeFrom(from, v.origin);
      v.message = message_decode(from);
    }

    TopicIndex topic_index;
    PeerIndex origin;
    MessagePtr message;
  };

  class Message : public IMessage {
   public:
    // IMessage
    MessageSize padding() const override {
      size_t padding = 0;
      for (auto &publish : this->publish) {
        padding += publish.message->padding();
      }
      return padding;
    }
    void encode(MessageEncodeTo &to) const override {
      encodeTo(to, publish);
      encodeTo(to, ihave);
      encodeTo(to, iwant);
    }
    static MessagePtr decode(MessageDecodeFrom &from) {
      auto message = std::make_shared<Message>();
      decodeFrom(from, message->publish);
      decodeFrom(from, message->ihave);
      decodeFrom(from, message->iwant);
      return message;
    }

    std::vector<Publish> publish;
    std::vector<MessageHash> ihave;
    std::vector<MessageHash> iwant;
  };
}  // namespace beamsim::gossip
