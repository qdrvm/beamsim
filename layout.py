import math


class Layout:
    def __init__(self):
        self.router_of_peer: list[int] = []
        self.routers_of_router: list[list[int]] = []
        self.wire_count: int = 0

    def build(self, simulator, make_wire):
        for _ in self.router_of_peer:
            simulator.add_peer()
        for _ in self.routers_of_router:
            simulator.add_router()
        for peer, router in enumerate(self.router_of_peer):
            simulator.wire_peer(peer, router, make_wire())
        for router1, routers in enumerate(self.routers_of_router):
            for router2 in routers:
                simulator.wire_router(router1, router2, make_wire())


def make_layout(peer_count: int, branch: int) -> Layout:
    """
    make balanced tree.
    leafs are peers.
    branch nodes are routers.
    """
    assert branch >= 2
    layout = Layout()
    assign_offset = 0
    next_layer = math.ceil(peer_count / branch)
    for i in range(peer_count):
        layout.router_of_peer.append(assign_offset + i % next_layer)
        layout.wire_count += 1
    while next_layer != 1:
        layer = next_layer
        assign_offset += layer
        next_layer = math.ceil(layer / branch)
        for i in range(layer):
            layout.routers_of_router.append([assign_offset + i % next_layer])
            layout.wire_count += 1
    layout.routers_of_router.append([])
    return layout
