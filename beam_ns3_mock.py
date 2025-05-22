"""
mock implementation
"""

import heapq

_peers = 0
_time = 0
_stop = False
_apps = dict()
_timers = []
_timer_nonce = 0


def add_peer():
    global _peers
    _peers += 1


def add_router():
    pass


def wire_peer(peer, router, wire):
    pass


def wire_router(router1, router2, wire):
    pass


def WireProps(bitrate, delay_ms):
    return None


class App:
    def __init__(self, index):
        self.index = index

    def on_start(self):
        pass

    def on_message(self, index, message):
        pass

    def connect(self, index):
        pass

    def send(self, index, message):
        message = bytearray(message)
        us = 10e3  # TODO: estimate time
        sleep_us(us, lambda: co_run(_apps[index].on_message(self.index, message)))

    def print(self, *args):
        print2(f"peer {self.index:2}:", *args)

    def without(self, peers):
        for index in peers:
            if index == self.index:
                continue
            yield index


def run(_App, timeout_sec):
    global _time
    for index in range(_peers):
        _apps[index] = _App(index)
    for app in _apps.values():
        co_run(app.on_start())
    while not _stop and _timers:
        if _time < _timers[0][0]:
            _time = _timers[0][0]
        if _time >= timeout_sec * 1e6:
            break
        heapq.heappop(_timers)[2]()


def stop():
    global _stop
    _stop = True


def now_us():
    return _time


def now_ms():
    return now_us() // 1000


def sleep_us(us, cb):
    us = int(us)
    global _timer_nonce
    heapq.heappush(_timers, (_time + us, _timer_nonce, cb))
    _timer_nonce += 1


def co_run(co):
    if co is None:
        return
    try:
        us = co.send(None)
        sleep_us(us, lambda: co_run(co))
    except StopIteration:
        pass


class co_sleep_us:
    def __init__(self, us):
        self.us = us

    def __await__(self):
        yield self.us


def print2(*args):
    print(f"{now_ms():6}ms: ", *args)
