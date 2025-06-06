#pragma once

#include <beamsim/assert.hpp>
#include <beamsim/peer_index.hpp>
#include <vector>

namespace beamsim::example {
  using GroupIndex = uint32_t;

  struct Roles {
    struct Group {
      std::vector<PeerIndex> validators;
      PeerIndex local_aggregator;
    };
    PeerIndex validator_count;
    std::vector<Group> groups;
    std::vector<PeerIndex> validators;
    std::vector<GroupIndex> group_of_validator;
    std::vector<PeerIndex> aggregators;
    PeerIndex global_aggregator;

    /**
     * split validators into groups.
     * assign local aggregator for group.
     * all peers are validators.
     * aggregators are subset of validators.
     * aggregator is either local or global.
     */
    static Roles make(PeerIndex validator_count, GroupIndex group_count) {
      assert2(group_count + 1 <= validator_count);
      Roles roles;
      roles.validator_count = validator_count;
      roles.group_of_validator.resize(validator_count);
      roles.global_aggregator = group_count;
      for (GroupIndex group_index = 0; group_index < group_count;
           ++group_index) {
        roles.groups.emplace_back(Group{.local_aggregator = group_index});
      }
      for (PeerIndex peer_index = 0; peer_index < group_count + 1;
           ++peer_index) {
        roles.aggregators.emplace_back(peer_index);
      }
      for (PeerIndex peer_index = 0; peer_index < validator_count;
           ++peer_index) {
        roles.validators.emplace_back(peer_index);
        GroupIndex group_index = peer_index % group_count;
        roles.groups.at(group_index).validators.emplace_back(peer_index);
        roles.group_of_validator.at(peer_index) = group_index;
      }
      return roles;
    }
  };
}  // namespace beamsim::example
