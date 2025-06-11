#pragma once

#include <beamsim/gossip/message.hpp>
#include <beamsim/gossip/view.hpp>
#include <beamsim/i_simulator.hpp>
#include <beamsim/std_hash.hpp>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace beamsim::gossip {
  class Peer {
   public:
    Peer(IPeer &peer) : peer_{peer} {}

    // TODO: ihave gossip timer

    void subscribe(TopicIndex topic_index, View &&view) {
      views_.emplace(topic_index, std::move(view));
    }

    void onMessage(PeerIndex from_peer,
                   const MessagePtr &any_message,
                   const auto &on_gossip) {
      auto &gossip_message = dynamic_cast<Message &>(*any_message);
      for (auto &publish : gossip_message.publish) {
        if (not views_.contains(publish.topic_index)) {
          continue;
        }
        auto message_hash = publish.message->hash();
        promises_.erase(message_hash);
        if (not duplicate_cache_.emplace(message_hash).second) {
          continue;
        }
        dontwant_.emplace(from_peer, message_hash);
        dontwant_.emplace(publish.origin, message_hash);
        _gossip(publish, message_hash);
        on_gossip(publish.message);
      }
      for (auto &message_hash : gossip_message.iwant) {
        auto it = mcache_.find(message_hash);
        if (it == mcache_.end()) {
          continue;
        }
        getBatch(from_peer).publish.emplace_back(it->second);
      }
      for (auto &message_hash : gossip_message.ihave) {
        if (duplicate_cache_.contains(message_hash)) {
          continue;
        }
        if (not promises_.emplace(message_hash).second) {
          continue;
        }
        getBatch(from_peer).iwant.emplace_back(message_hash);
      }
    }

    void gossip(TopicIndex topic_index, MessagePtr any_message) {
      auto message_hash = any_message->hash();
      if (not duplicate_cache_.emplace(message_hash).second) {
        return;
      }
      _gossip({topic_index, peer_.peer_index_, any_message}, message_hash);
    }

   private:
    void _gossip(const Publish &publish, MessageHash message_hash) {
      mcache_.emplace(message_hash, publish);
      // TODO: add to ihave gossip queue
      for (auto &to_peer : views_.at(publish.topic_index).publishTo()) {
        if (dontwant_.contains({to_peer, message_hash})) {
          continue;
        }
        getBatch(to_peer).publish.emplace_back(publish);
      }
    }

    Message &getBatch(PeerIndex to_peer) {
      if (batches_.empty()) {
        peer_.simulator_.runSoon([this] {
          auto batches = std::exchange(batches_, {});
          for (auto &[to_peer, message] : batches) {
            peer_.send(to_peer, message);
          }
        });
      }
      auto &batch = batches_[to_peer];
      if (not batch) {
        batch = std::make_shared<Message>();
      }
      return *batch;
    }

    IPeer &peer_;
    std::unordered_map<TopicIndex, View> views_;
    std::unordered_set<MessageHash> duplicate_cache_;
    std::unordered_set<std::pair<PeerIndex, MessageHash>, PairHash> dontwant_;
    std::map<PeerIndex, std::shared_ptr<Message>> batches_;
    std::unordered_set<MessageHash> promises_;
    std::unordered_map<MessageHash, Publish> mcache_;
  };
}  // namespace beamsim::gossip
