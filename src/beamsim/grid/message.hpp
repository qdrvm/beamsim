#pragma once

#include <beamsim/message.hpp>

namespace beamsim::grid {
  // hack
  MessageDecodeFn message_decode;

  using Ttl = uint8_t;

  class Message : public IMessage {
   public:
    Message() = default;
    Message(MessagePtr message, Ttl ttl)
        : message{std::move(message)}, ttl{ttl} {}

    // IMessage
    MessageSize padding() const override {
      return message->padding();
    }
    void encode(MessageEncodeTo &to) const override {
      encodeTo(to, ttl);
      message->encode(to);
    }
    static MessagePtr decode(MessageDecodeFrom &from) {
      auto message = std::make_shared<Message>();
      decodeFrom(from, message->ttl);
      message->message = message_decode(from);
      return message;
    }

    MessagePtr message;
    Ttl ttl;
  };
}  // namespace beamsim::grid
