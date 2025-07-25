import json
import subprocess
import os
import warnings
import numpy as np
import tempfile
import hashlib


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


def get_snark1_sent(items):
    rows = filter_report(items, "snark1_sent")
    xs, ys = [0], [0]
    groups = dict()
    for t, _, g, n2 in rows:
        assert t != 0
        n1 = groups.get(g, 0)
        if n2 <= n1:
            continue
        groups[g] = n2
        xs.append(t)
        ys.append(ys[-1] + (n2 - n1))
    return xs, ys


def get_snark1_received(items):
    rows = filter_report(items, "snark1_received")
    rows2 = list()
    for row in rows:
        if not rows2 or row[2] > rows2[-1][2]:
            rows2.append(row)
    return rows2


def get_signature_duplicates(items):
    _, _, duplicates, signatures = filter_report(items, "signature-duplicates")[0]
    avg_duplicates = duplicates / signatures
    return duplicates, avg_duplicates


class Metrics:
    def __init__(self, items):
        rows = filter_report(items, "metrics")
        _, _, *roles = filter_report(items, "metrics-roles")[0]
        self.t = max(len(row[5]) for row in rows)
        self.a = np.zeros((2, 3, 2, self.t))
        for _, _, i1, i2, i3, bucket in rows:
            self.a[i1, i2, i3, : len(bucket)] += bucket
        self.messages_received_role = [np.cumsum(self.a[0][i][0]) for i in range(3)]
        self.messages_received_all = np.sum(self.messages_received_role, axis=0)
        self.messages_sent_role = [np.cumsum(self.a[0][i][1]) for i in range(3)]
        self.messages_sent_all = np.sum(self.messages_sent_role, axis=0)
        self.bytes_received_role = [np.cumsum(self.a[1][i][0]) for i in range(3)]
        self.bytes_received_all = np.sum(self.bytes_received_role, axis=0)
        self.bytes_received_role_avg = [
            a / n for a, n in zip(self.bytes_received_role, roles)
        ]
        self.bytes_sent_role = [np.cumsum(self.a[1][i][1]) for i in range(3)]
        self.bytes_sent_all = np.sum(self.bytes_sent_role, axis=0)
        self.bytes_sent_role_avg = [a / n for a, n in zip(self.bytes_sent_role, roles)]


topology_name = {
    "direct": "Direct",
    "gossip": "Gossip",
    "grid": "Grid",
}
topologies = list(topology_name.keys())
role_name = [
    "Validator",
    "Local Aggregator",
    "Global Aggregator",
]

exe = "build/beamsim"
if not os.path.exists(exe):  # for docker build
    exe = "/usr/local/bin/beamsim"
    if not os.path.exists(exe):
        raise FileNotFoundError(
            f"Executable {exe} not found. Please build the project first."
        )

run_cache = dict()
run_exe_time = None


def run(
    b=None, t=None, g=None, gv=None, shuffle=False, mpi=False, c=None, la=None, ga=None, local_aggregation_only=False
):
    if not isinstance(mpi, bool) and mpi > os.cpu_count():
        warnings.warn(
            f"beamsim.run requested mpi {mpi} exceeds os cpu count {os.cpu_count()}"
        )
        mpi = os.cpu_count()
    if c is None:
        if b is None:
            b = "ns3"
        if t is None:
            t = "direct"
        if g is None:
            g = 10
        if gv is None:
            gv = 10
    global run_exe_time
    exe_time = os.stat(exe).st_mtime
    if run_exe_time != exe_time:
        run_exe_time = exe_time
        run_cache.clear()
    c_key = None if c is None else (c, os.stat(c).st_mtime)
    key = (b, t, g, gv, shuffle, mpi, c_key, la, ga, local_aggregation_only)
    output = run_cache.get(key, None)
    if output is None:
        cmd = [
            *(
                (["mpirun"] if mpi else [])
                if isinstance(mpi, bool)
                else ["mpirun", "-n", str(mpi)]
            ),
            exe,
            *([] if c is None else ["-c", c]),
            *([] if b is None else ["-b", b]),
            *([] if t is None else ["-t", t]),
            *([] if g is None else ["-g", str(g)]),
            *([] if gv is None else ["-gv", str(gv)]),
            *(["--shuffle"] if shuffle else []),
            *(["-la", str(la)] if la is not None else []),
            *(["-ga", str(ga)] if ga is not None else []),
            *(["--local-aggregation-only"] if local_aggregation_only else []),
            "--report",
        ]
        print(f"run: {' '.join(cmd)}")
        output = subprocess.check_output(cmd, text=True)
        run_cache[key] = output
    return parse_report(output)


yaml_dir = None


def yaml(yaml: str):
    global yaml_dir
    if yaml_dir is None:
        yaml_dir = os.path.join(tempfile.gettempdir(), "beamsim-yaml-md5")
        os.makedirs(yaml_dir, exist_ok=True)
    md5 = hashlib.md5(yaml.encode()).hexdigest()
    path = os.path.join(yaml_dir, md5)
    if not os.path.exists(path):
        with open(path, "wb") as file:
            file.write(yaml.encode())
    return path
