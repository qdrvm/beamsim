import beam_ns3
# import beam_ns3_mock as beam_ns3

from roles import make_roles
from layout import make_layout

import pickle

"""
validator generates signature.
validator sends signature it to group's local aggregator.

local aggregator aggregates multiple signatures from it's group into snark1.
local aggregator may share signatures with other group's local aggregators.
local aggregator sends snark1 to group's global aggregator.
local aggregator sends snark2 to group's validators.

global aggregator aggregates multiple snark1 into snark2.
global aggregator may share snark1 with other group's global aggregators.
global aggregator sends snark2 to local aggregators.
"""

signature_size = 1536
signature_us = 20e3
snark1_size = 131072
snark1_us = 200e3
snark2_size = snark1_size
snark2_us = 300e3


class MessageSignature:
    def __init__(self, index: int):
        self.index = index
        self.padding = b"x" * signature_size


class MessageSnark1:
    def __init__(self, group: int, indices: set[int]):
        self.group = group
        self.indices = indices
        self.padding = b"x" * snark1_size


class MessageSnark2:
    def __init__(self):
        self.padding = b"x" * snark2_size


class App(beam_ns3.App):
    def __init__(self, index):
        super().__init__(index)
        self.group = roles.get_group(self.index)
        self.is_validator = self.group is not None
        self.is_local_aggregator = (
            self.group and self.index in self.group[1].local_aggregators
        )
        self.is_global_aggregator = self.index in roles.global_aggregators
        self.signature_indices = set()
        self.signatures_threshold = len(self.group[1].validators)
        self.snark1_indices = set()
        self.snark1_threshold = peer_count
        self.have_snark2 = False

    async def on_start(self):
        if not self.is_validator:
            return
        if not self.is_global_aggregator:
            for index in self.without(self.group[1].local_aggregators):
                self.connect(index)
        if self.is_local_aggregator:
            for index in self.without(roles.global_aggregators):
                self.connect(index)

        self.print("generating signature")
        await beam_ns3.co_sleep_us(signature_us)

        message = MessageSignature(self.index)
        await self.add_signature(message.index)
        for index in self.without(self.group[1].local_aggregators):
            self.print(f"send signature to {index:2}")
            self.send(index, pickle.dumps(message))

    async def on_message(self, index, message_bytes):
        message = pickle.loads(message_bytes)
        if isinstance(message, MessageSignature):
            assert self.is_local_aggregator
            await self.add_signature(message.index)
            # TODO: share with local aggregators
            return
        if isinstance(message, MessageSnark1):
            assert self.is_global_aggregator
            await self.add_snark1(message.group, message.indices)
            # TODO: share with global aggregators
            return
        if isinstance(message, MessageSnark2):
            self.print(f"got snark2 from {index:2}")
            if self.is_local_aggregator:
                for index in self.without(self.group[1].validators):
                    if index in self.group[1].local_aggregators:
                        continue
                    self.print(f"send snark2 to {index:2}")
                    self.send(index, message_bytes)
            self.add_snark2()
            return

    async def add_signature(self, index):
        if not self.is_local_aggregator:
            return
        self.signature_indices.add(index)
        if len(self.signature_indices) < self.signatures_threshold:
            return

        self.print("generating snark1")
        await beam_ns3.co_sleep_us(snark1_us)

        message = MessageSnark1(self.group[0], self.signature_indices)
        await self.add_snark1(message.group, message.indices)
        for index in self.without(roles.global_aggregators):
            self.print(f"send snark1 to {index:2}")
            self.send(index, pickle.dumps(message))

    async def add_snark1(self, group, signature_indices):
        if not self.is_global_aggregator:
            return
        self.snark1_indices.update(signature_indices)
        if len(self.snark1_indices) < self.snark1_threshold:
            return

        self.print("generating snark2")
        await beam_ns3.co_sleep_us(snark2_us)

        message = MessageSnark2()
        self.add_snark2()
        for group in roles.groups:
            for index in self.without(group.local_aggregators):
                self.print(f"send snark2 to {index:2}")
                self.send(index, pickle.dumps(message))

    def add_snark2(self):
        if self.have_snark2:
            return
        self.have_snark2 = True
        global have_snark2
        have_snark2 += 1
        if have_snark2 < peer_count:
            return
        beam_ns3.print2("all snark2")
        beam_ns3.stop()


peer_count = 100
roles = make_roles(peer_count, 10, 10, 1)
layout = make_layout(peer_count, 3)
layout.build(beam_ns3, lambda: beam_ns3.WireProps(1 << 23, 10))

have_snark2 = 0

beam_ns3.run(App, 60)
