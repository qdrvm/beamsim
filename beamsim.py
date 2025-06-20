import json
import subprocess
import os
import numpy as np


def parse_report(lines):
    if isinstance(lines, str):
        lines = lines.splitlines()
    items = []
    for line in lines:
        if not line.startswith('["report",'):
            continue
        _, *args = json.loads(line)
        items.append(args)
    items.sort(key=lambda x: x[0])
    return items


def filter_report(items, type_):
    return [x for x in items if x[1] == type_]


def time_axis(items):
    return [x[0] for x in items]


def get_snark1_received(items):
    rows = filter_report(items, "snark1_received")
    for i in reversed(range(1, len(rows))):
        if rows[i][2] <= rows[i - 1][2]:
            rows.pop(i)
    return rows


class Metrics:
    def __init__(self, items):
        rows = filter_report(items, "metrics")
        _, _, *roles = filter_report(items, "metrics-roles")[0]
        self.t = max(len(row[5]) for row in rows)
        self.a = np.zeros((2, 3, 2, self.t))
        for _, _, i1, i2, i3, bucket in rows:
            self.a[i1, i2, i3, : len(bucket)] += bucket
        self.messages_sent_role = [np.cumsum(self.a[0][i][1]) for i in range(3)]
        self.messages_sent_all = np.sum(self.messages_sent_role, axis=0)
        self.bytes_sent_role = [np.cumsum(self.a[1][i][1]) for i in range(3)]
        self.bytes_sent_all = np.sum(self.bytes_sent_role, axis=0)
        self.bytes_sent_role_avg = [a / n for a, n in zip(self.bytes_sent_role, roles)]


exe = "build/main"

run_cache = dict()
run_exe_time = None


def run(b="ns3", t="direct", g=10, gv=10, shuffle=False, mpi=False):
    global run_exe_time
    exe_time = os.stat(exe).st_mtime
    if run_exe_time != exe_time:
        run_exe_time = exe_time
        run_cache.clear()
    key = (b, t, g, gv, shuffle, mpi)
    output = run_cache.get(key, None)
    if output is None:
        cmd = [
            *(
                (["mpirun"] if mpi else [])
                if isinstance(mpi, bool)
                else ["mpirun", "-n", str(mpi)]
            ),
            exe,
            "-b",
            b,
            "-t",
            t,
            "-g",
            str(g),
            "-gv",
            str(gv),
            *(["--shuffle"] if shuffle else []),
        ]
        print(f"run: {' '.join(cmd)}")
        output = subprocess.check_output(cmd, text=True)
        run_cache[key] = output
    return parse_report(output)
