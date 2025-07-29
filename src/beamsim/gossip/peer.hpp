#pragma once

#include <beamsim/gossip/config.hpp>
#include <beamsim/gossip/message.hpp>
#include <beamsim/gossip/view.hpp>
#include <beamsim/i_simulator.hpp>
#include <beamsim/std_hash.hpp>
#include <beamsim/example/message.hpp>
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
    
    // Get the current signature bitfield for this peer (for idontwant mode)
    const beamsim::example::BitSet& getSignatureBitfield() const {
      auto it = peer_signatures_.find(peer_.peer_index_);
      if (it != peer_signatures_.end()) {
        return it->second;
      }
      static const beamsim::example::BitSet empty_bitset;
      return empty_bitset;
    }
    
    // Update our own signature bitfield when we see a new signature
    void updateOwnSignature(PeerIndex signature_peer_index) {
      peer_signatures_[peer_.peer_index_].set(signature_peer_index);
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
          
          // For signature messages, extract signature information and update peer knowledge
          if (isSignatureMessage(publish.message)) {
            try {
              auto &example_message = dynamic_cast<const beamsim::example::Message &>(*publish.message);
              if (auto *signature = std::get_if<beamsim::example::MessageSignature>(&example_message.variant)) {
                // Update our knowledge that the sender has this signature
                peer_signatures_[from_peer].set(signature->peer_index);
                
                // If the message contains piggybacked signature information, update our knowledge
                if (signature->seen_signatures.has_value()) {
                  // Update our knowledge of what signatures the sender has
                  updatePeerSignatureKnowledge(from_peer, signature->seen_signatures.value());
                }
                
                // When forwarding, add our own signature bitfield to help others know what we have
                // This is the "piggybacking" part - we attach our signature knowledge when forwarding
                attachSignatureBitfieldToForward(publish.topic_index);
              }
            } catch (const std::bad_cast&) {
              // Not an example message, ignore
            }
          }
        }
        promises_.erase(message_hash);
        if (not duplicate_cache_.emplace(message_hash).second) {
          continue;
        }
        dontwant_.emplace(from_peer, message_hash);
        dontwant_.emplace(publish.origin, message_hash);
        on_gossip(publish.message, [this, publish, message_hash] {
          _gossip(publish, message_hash);
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
      auto message_hash = any_message->hash();
      if (not duplicate_cache_.emplace(message_hash).second) {
        return;
      }
      
      // Check if this is a signature message and handle bitfield piggybacking
      if (config_.idontwant && isSignatureMessage(any_message)) {
        handleSignaturePiggybacking(topic_index, any_message);
      }
      
      _gossip({topic_index, peer_.peer_index_, any_message}, message_hash);
    }

   private:
    bool isSignatureMessage(const MessagePtr &message) {
      // Check if the message is a signature message from beamsim::example
      // We need to cast to check the variant
      try {
        auto &example_message = dynamic_cast<const beamsim::example::Message &>(*message);
        return std::holds_alternative<beamsim::example::MessageSignature>(example_message.variant);
      } catch (const std::bad_cast&) {
        return false;
      }
    }
    
    void handleSignaturePiggybacking(TopicIndex /*topic_index*/, const MessagePtr &message) {
      // Update our knowledge of signatures when we see a signature message
      try {
        auto &example_message = dynamic_cast<const beamsim::example::Message &>(*message);
        if (auto *signature = std::get_if<beamsim::example::MessageSignature>(&example_message.variant)) {
          // Track which signatures this peer has seen
          auto peer_index = signature->peer_index;
          peer_signatures_[peer_.peer_index_].set(peer_index);
          
          // If the message has signature bitfield information, update our knowledge
          if (signature->seen_signatures.has_value()) {
            peer_signatures_[peer_.peer_index_].set(signature->seen_signatures.value());
          }
        }
      } catch (const std::bad_cast&) {
        // Not an example message, ignore
      }
    }
    
    void updatePeerSignatureKnowledge(PeerIndex from_peer, const beamsim::example::BitSet &signature_bitfield) {
      // Update our knowledge of what signatures the sending peer has
      peer_signatures_[from_peer] = signature_bitfield;
    }
    
    bool shouldSendSignatureToPeer(PeerIndex to_peer, PeerIndex signature_peer_index) {
      // Check if the target peer already has this signature
      auto it = peer_signatures_.find(to_peer);
      if (it != peer_signatures_.end()) {
        return !it->second.get(signature_peer_index);
      }
      // If we don't know what signatures they have, send it
      return true;
    }

    void _gossip(const Publish &publish, MessageHash message_hash) {
      mcache_.emplace(message_hash, publish);
      history_[publish.topic_index].add(message_hash);
      
      // size_t total_peers = views_.at(publish.topic_index).publishTo().size();
      size_t skipped_peers = 0;
      
      for (auto &to_peer : views_.at(publish.topic_index).publishTo()) {
        if (dontwant_.contains({to_peer, message_hash})) {
          continue;
        }
        
        // For signature messages with idontwant enabled, check if peer already has the signature
        if (config_.idontwant && isSignatureMessage(publish.message)) {
          try {
            auto &example_message = dynamic_cast<const beamsim::example::Message &>(*publish.message);
            if (auto *signature = std::get_if<beamsim::example::MessageSignature>(&example_message.variant)) {
              if (!shouldSendSignatureToPeer(to_peer, signature->peer_index)) {
                skipped_peers++;
                continue; // Skip sending this signature to this peer
              }
            }
          } catch (const std::bad_cast&) {
            // Not an example message, proceed normally
          }
        }
        
        getBatch(to_peer).publish.emplace_back(publish);
      }
      
      // Report on idontwant efficiency for signature messages
      if (config_.idontwant && isSignatureMessage(publish.message) && skipped_peers > 0) {
        // This would be reported if we had access to the reporting mechanism
        // report("signature_idontwant_skipped", skipped_peers, total_peers);
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
      for (auto &[topic, history] : history_) {
        history.shift([this](const MessageHash &message_hash) {
          mcache_.erase(message_hash);
        });
      }
    }

    void attachSignatureBitfieldToForward(TopicIndex /*topic_index*/) {
      // When forwarding signature messages, we can piggyback our own signature bitfield
      // to inform other peers about which signatures we have
      // This helps with the idontwant optimization for signatures
      
      // For now, we rely on the existing peer_signatures_ tracking
      // In a full implementation, we would add this to the gossip message
      // but for this approach we track it separately
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
    // Track which signatures each peer has for idontwant mode
    std::unordered_map<PeerIndex, beamsim::example::BitSet> peer_signatures_;
  };
}  // namespace beamsim::gossip
