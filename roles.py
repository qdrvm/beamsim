class RolesGroup:
    def __init__(self):
        self.validators: list[int] = []
        self.local_aggregators: list[int] = []
        self.global_aggregators: list[int] = []


class Roles:
    def __init__(self):
        self.groups: list[RolesGroup] = []
        self.global_aggregators: list[int] = []

    def get_group(self, index):
        return next(
            (i, group)
            for i, group in enumerate(self.groups)
            if index in group.validators
        )


def make_roles(
    validator_count: int,
    group_count: int,
    local_aggregator_count: int,
    global_aggregator_count: int,
) -> Roles:
    """
    split validators to groups.
    assign local aggregator to groups.
    assign global aggregator for group.
    all peers are validators.
    aggregators are subset of validators.
    aggregator is either local or global.
    """
    assert group_count <= validator_count
    assert group_count <= local_aggregator_count
    assert local_aggregator_count + global_aggregator_count <= validator_count
    roles = Roles()
    roles.groups = [RolesGroup() for _ in range(group_count)]
    roles.global_aggregators = [
        local_aggregator_count + i for i in range(global_aggregator_count)
    ]
    for i in range(validator_count):
        roles.groups[i % group_count].validators.append(i)
    for i in range(local_aggregator_count):
        roles.groups[i % group_count].local_aggregators.append(i)
    assert global_aggregator_count <= group_count  # TODO
    for i in range(group_count):
        roles.groups[i].global_aggregators.append(
            roles.global_aggregators[i % global_aggregator_count]
        )
    return roles
