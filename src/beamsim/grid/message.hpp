#pragma once

#include <beamsim/message.hpp>

namespace beamsim::grid {
  using Ttl = uint8_t;

  class Message : public IMessage {
   public:
    Message() = default;
    Message(MessagePtr message, Ttl ttl)
        : message{std::move(message)}, ttl{ttl} {}

    // IMessage
    MESSAGE_TYPE_INDEX;
    MessageSize padding() const override {
      return message->padding();
    }
    void encode(MessageEncodeTo &to) const override {
      IMessage::encode(to);
      encodeTo(to, ttl);
      message->encode(to);
    }
    static MessagePtr decode(MessageDecodeFrom &from) {
      auto message = std::make_shared<Message>();
      decodeFrom(from, message->ttl);
      message->message = decodeMessage(from);
      return message;
    }

    MessagePtr message;
    Ttl ttl;
  };
}  // namespace beamsim::grid
