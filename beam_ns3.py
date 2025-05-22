import beam_ns3_install

from ns import ns
import cppyy

cppyy.include("beam_ns3.hpp")
beam_ns3 = cppyy.gbl.beam_ns3

reexport = ["WireProps", "add_peer", "add_router", "wire_peer", "wire_router"]
globals().update({k: getattr(beam_ns3, k) for k in reexport})


def catch(f):
    def w(*args, **kwargs):
        try:
            return f(*args, **kwargs)
        except Exception as e:
            print(e)
            exit(-1)

    return w


class App:
    def __init__(self, index):
        self.index = index

    def on_start(self):
        pass

    def on_message(self, index, message):
        pass

    def connect(self, index):
        beam_ns3.socket_connect(self.index, index)

    def send(self, index, message):
        message = bytearray(message)
        beam_ns3.socket_send(self.index, index, message)

    def print(self, *args):
        print2(f"peer {self.index:2}:", *args)

    def without(self, peers):
        for index in peers:
            if index == self.index:
                continue
            yield index


class CPy(beam_ns3.CPy):
    app = None
    next_timer_id = 0
    timers = dict()

    def __init__(self):
        super().__init__()
        self.apps = dict()

    @catch
    def on_start(self, peer):
        app = CPy.app(peer)
        self.apps[peer] = app
        co_run(app.on_start())

    @catch
    def on_message(self, peer, from_peer, message):
        message = bytearray([ord(x) for x in message])
        app = self.apps[peer]
        co_run(app.on_message(from_peer, message))

    @catch
    def on_timer(self, timer_id):
        cb = CPy.timers.pop(timer_id)
        cb()


def run(app, timeout_sec):
    CPy.app = app
    beam_ns3.run(CPy(), timeout_sec)


def stop():
    ns.Simulator.Stop()


# TODO: Now() returns 0 after Destroy()
def now_us():
    return ns.Simulator.Now().GetMicroSeconds()


def now_ms():
    return now_us() // 1000


def sleep_us(us, cb):
    timer_id = CPy.next_timer_id
    CPy.next_timer_id += 1
    CPy.timers[timer_id] = cb
    beam_ns3.sleep(int(us), timer_id)


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
