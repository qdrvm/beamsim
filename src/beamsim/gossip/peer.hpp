#pragma once

#include <beamsim/gossip/config.hpp>
#include <beamsim/gossip/message.hpp>
#include <beamsim/gossip/view.hpp>
#include <beamsim/i_simulator.hpp>
#include <beamsim/std_hash.hpp>
#include <map>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace beamsim::gossip {
  constexpr std::chrono::seconds kHeartbeatInterval{1};
  constexpr size_t kHistoryGossip = 3;
  constexpr size_t kHistoryLength = kHistoryGossip + 2;
  constexpr size_t kMaxIhave = 5000;

  struct History {
    History() {
      buckets_.resize(kHistoryLength, 0);
    }

    void shift(const auto &on_remove) {
      auto n = buckets_.front();
      for (auto &message_hash : std::span{messages_}.first(n)) {
        on_remove(message_hash);
      }
      messages_.erase(messages_.begin(), messages_.begin() + n);
      buckets_.erase(buckets_.begin());
      buckets_.emplace_back(0);
    }

    auto gossipAbout(Random &random) const {
      size_t offset = 0;
      for (size_t i = 0; i < kHistoryLength - kHistoryGossip; ++i) {
        offset += buckets_[i];
      }
      return random.sample(std::span{messages_}.subspan(offset), kMaxIhave);
    }

    void add(const MessageHash &message_hash) {
      messages_.emplace_back(message_hash);
      ++buckets_.back();
    }

    std::vector<MessageHash> messages_;
    std::vector<size_t> buckets_;
  };

  class Peer {
   public:
    Peer(IPeer &peer, Random &random, Config &config)
        : peer_{peer}, random_{random}, config_{config} {}

    void start() {
      setTimerHeartbeat();
    }

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
        if (config_.idontwant) {
          for (auto &to_peer : views_.at(publish.topic_index).publishTo()) {
            if (to_peer == from_peer) {
              continue;
            }
            getBatch(to_peer).idontwant.emplace_back(message_hash);
          }
        }
        promises_.erase(message_hash);
        if (not duplicate_cache_.emplace(message_hash).second) {
          continue;
        }
        dontwant_.emplace(from_peer, message_hash);
        dontwant_.emplace(publish.origin, message_hash);
        on_gossip(publish.message, [this, publish, message_hash] {
          _gossip(publish,
                  message_hash,
                  views_.at(publish.topic_index).publishTo());
        });
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
      for (auto &message_hash : gossip_message.idontwant) {
        if (duplicate_cache_.contains(message_hash)) {
          continue;
        }
        dontwant_.emplace(from_peer, message_hash);
      }
    }

    void gossip(TopicIndex topic_index, MessagePtr any_message) {
      gossip(topic_index, any_message, views_.at(topic_index).publishTo());
    }

    void gossip(TopicIndex topic_index,
                MessagePtr any_message,
                const auto &peers) {
      auto message_hash = any_message->hash();
      if (not duplicate_cache_.emplace(message_hash).second) {
        return;
      }
      _gossip(
          {topic_index, peer_.peer_index_, any_message}, message_hash, peers);
    }

   private:
    void _gossip(const Publish &publish,
                 MessageHash message_hash,
                 const auto &peers) {
      mcache_.emplace(message_hash, publish);
      history_[publish.topic_index].add(message_hash);
      for (auto &to_peer : peers) {
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

    void setTimerHeartbeat() {
      peer_.simulator_.runAfter(kHeartbeatInterval, [this] {
        setTimerHeartbeat();
        onHeartbeat();
      });
    }

    void onHeartbeat() {
      for (auto &[topic_index, view] : views_) {
        auto it = history_.find(topic_index);
        if (it == history_.end()) {
          continue;
        }
        auto &history = it->second;
        for (auto &to_peer : view.ihaveTo(random_)) {
          for (auto &message_hash : history.gossipAbout(random_)) {
            getBatch(to_peer).ihave.emplace_back(message_hash);
          }
        }
      }
      for (auto &history : history_ | std::views::values) {
        history.shift([this](const MessageHash &message_hash) {
          mcache_.erase(message_hash);
        });
      }
    }

    IPeer &peer_;
    Random &random_;
    Config &config_;
    std::unordered_map<TopicIndex, View> views_;
    std::unordered_set<MessageHash> duplicate_cache_;
    std::unordered_set<std::pair<PeerIndex, MessageHash>, PairHash> dontwant_;
    std::map<PeerIndex, std::shared_ptr<Message>> batches_;
    std::unordered_set<MessageHash> promises_;
    std::unordered_map<MessageHash, Publish> mcache_;
    std::unordered_map<TopicIndex, History> history_;
  };
}  // namespace beamsim::gossip
