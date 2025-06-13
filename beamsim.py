import json
import subprocess
import os


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


exe = "build/main"

run_cache = dict()
run_exe_time = None


def run(b="ns3", t="direct", g=10, gv=10):
    global run_exe_time
    exe_time = os.stat(exe).st_mtime
    if run_exe_time != exe_time:
        run_exe_time = exe_time
        run_cache.clear()
    key = (b, t, g, gv)
    output = run_cache.get(key, None)
    if output is None:
        cmd = [exe, "-b", b, "-t", t, "-g", str(g), "-gv", str(gv)]
        print(f"run: {' '.join(cmd)}")
        output = subprocess.check_output(cmd, text=True)
        run_cache[key] = output
    return parse_report(output)
