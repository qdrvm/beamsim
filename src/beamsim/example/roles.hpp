#pragma once

#include <beamsim/assert.hpp>
#include <beamsim/peer_index.hpp>
#include <unordered_map>
#include <vector>

namespace beamsim::example {
  using GroupIndex = uint32_t;

  using IndexOfPeerMap = std::unordered_map<PeerIndex, PeerIndex>;

  enum class Role : uint8_t { Validator, LocalAggregator, GlobalAggregator };

  struct RolesConfig {
    GroupIndex group_count = 4;
    PeerIndex group_validator_count = 4;  // including aggregators
    PeerIndex global_aggregator_count = 1;
    PeerIndex group_local_aggregator_count = 1;
  };

  struct Roles {
    struct Group {
      std::vector<PeerIndex> validators;
      IndexOfPeerMap index_of_validators;
      std::vector<PeerIndex> local_aggregators;
    };
    PeerIndex validator_count;
    std::vector<Group> groups;
    std::vector<PeerIndex> validators;
    std::vector<GroupIndex> group_of_validator;
    std::vector<Role> roles;
    std::vector<PeerIndex> aggregators;
    IndexOfPeerMap index_of_aggregators;
    std::vector<PeerIndex> global_aggregators;

    /**
     * split validators into groups.
     * assign local aggregator for group.
     * all peers are validators.
     * aggregators are subset of validators.
     * aggregator is either local or global.
     */
    static Roles make(RolesConfig config) {
      auto local_aggregator_count =
          config.group_count * config.group_local_aggregator_count;
      auto aggregator_count =
          local_aggregator_count + config.global_aggregator_count;
      auto validator_count = config.group_count * config.group_validator_count;
      assert2(config.group_count >= 1);
      assert2(config.global_aggregator_count >= 1);
      assert2(config.group_local_aggregator_count >= 1);
      assert2(config.group_validator_count >= 1);
      assert2(aggregator_count <= validator_count);
      Roles roles;
      roles.validator_count = validator_count;
      roles.group_of_validator.resize(validator_count);
      roles.groups.resize(config.group_count);
      for (PeerIndex peer_index = 0; peer_index < aggregator_count;
           ++peer_index) {
        roles.index_of_aggregators.emplace(peer_index,
                                           roles.aggregators.size());
        roles.aggregators.emplace_back(peer_index);
      }
      for (PeerIndex peer_index = 0; peer_index < local_aggregator_count;
           ++peer_index) {
        GroupIndex group_index = peer_index % config.group_count;
        roles.groups.at(group_index).local_aggregators.emplace_back(peer_index);
      }
      for (PeerIndex peer_index = local_aggregator_count;
           peer_index < aggregator_count;
           ++peer_index) {
        roles.global_aggregators.emplace_back(peer_index);
      }
      roles.roles.reserve(validator_count);
      roles.roles.resize(local_aggregator_count, Role::LocalAggregator);
      roles.roles.resize(aggregator_count, Role::GlobalAggregator);
      roles.roles.resize(validator_count, Role::Validator);
      for (PeerIndex peer_index = 0; peer_index < validator_count;
           ++peer_index) {
        roles.validators.emplace_back(peer_index);
        GroupIndex group_index = peer_index % config.group_count;
        auto &group = roles.groups.at(group_index);
        group.index_of_validators.emplace(peer_index, group.validators.size());
        group.validators.emplace_back(peer_index);
        roles.group_of_validator.at(peer_index) = group_index;
      }
      return roles;
    }
  };
}  // namespace beamsim::example
