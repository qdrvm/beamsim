#pragma once

#include <beamsim/gossip/topic_index.hpp>
#include <beamsim/message.hpp>
#include <beamsim/peer_index.hpp>

namespace beamsim::gossip {
  struct Publish {
    TopicIndex topic_index;
    PeerIndex origin;
    MessagePtr message;
  };

  class Message : public IMessage {
   public:
    // IMessage
    size_t size() const override {
      size_t size = 0;
      size += publish.size() * sizeof(Publish::topic_index);
      size += publish.size() * sizeof(Publish::origin);
      for (auto &publish : this->publish) {
        size += publish.message->size();
      }
      size += ihave.size() * sizeof(MessageHash);
      size += iwant.size() * sizeof(MessageHash);
      return size;
    }
    void hash(MessageHasher &) const override {
      abort();
    }

    std::vector<Publish> publish;
    std::vector<MessageHash> ihave;
    std::vector<MessageHash> iwant;
  };
}  // namespace beamsim::gossip
