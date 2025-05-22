import subprocess
import sys
import platform
import os
import venv

VENV = "venv"
PIP = os.path.join(VENV, "bin/pip3")
PYTHON = os.path.join(VENV, "bin/python3")

NS3_VERSION = "3.44"


def is_macos():
    return sys.platform == "darwin"


def is_amd64():
    return platform.machine() == "AMD64"


# https://stackoverflow.com/a/1883251
def is_venv():
    return sys.prefix != sys.base_prefix


def env_path_append(name, path):
    s = os.environ.get(name, None)
    paths = s.split(os.pathsep) if s else []
    paths.append(path)
    os.environ[name] = os.pathsep.join(paths)


# https://www.nsnam.org/docs/release/3.44/installation/html/quick-start.html
def build_from_source():
    name1 = f"ns-allinone-{NS3_VERSION}"
    dir1 = os.path.join(name1, f"ns-{NS3_VERSION}")
    if not os.path.isdir(name1):
        name_archive = name1 + ".tar.bz2"
        if not os.path.isfile(name_archive):
            name_archive_tmp = f"{name_archive}.tmp"
            url = f"https://www.nsnam.org/releases/{name_archive}"
            print(f"downloading {url}")
            subprocess.check_call(
                [
                    "curl",
                    "-L",
                    "-o",
                    name_archive_tmp,
                    url,
                ]
            )
            os.rename(name_archive_tmp, name_archive)
        subprocess.check_call(["tar", "-xf", name_archive])
    dir2 = os.path.join(dir1, "build/bindings/python")
    if not os.path.isdir(dir2):
        print("installing ns3...")
        cli = os.path.join(dir1, "ns3")
        subprocess.check_call(
            [
                cli,
                "configure",
                "--enable-python-bindings",
            ]
        )
        subprocess.check_call([cli, "build"])
    sys.path.append(dir2)
    env_path_append("PYTHONPATH", dir2)


if not is_venv():
    if not os.path.isdir(VENV):
        print("creating venv...")
        venv.create(VENV, with_pip=True)
    print("warning: restarting with venv...")
    cmd = [PYTHON, *sys.argv]
    print(f'  run via "{" ".join(cmd)}" to avoid this warning')
    subprocess.check_call(cmd)
    exit(0)

if is_macos():
    dir1 = "/opt/local/lib"
    zstd_link = os.path.join(dir1, "libzstd.1.dylib")
    if not os.path.exists(zstd_link):
        cmd1 = ["sudo", "mkdir", "-p", dir1]
        print(" ".join(cmd1))
        subprocess.check_call(cmd1)
        cmd2 = [
            "sudo",
            "ln",
            "-s",
            "/opt/homebrew/lib/libzstd.1.dylib",
            zstd_link,
        ]
        print(" ".join(cmd2))
        subprocess.check_call(cmd2)

try:
    import cppyy
except ModuleNotFoundError:
    print("installing cppyy...")
    subprocess.check_call([PIP, "install", "cppyy"])
import cppyy

try:
    import ns
except ModuleNotFoundError:
    if is_macos() or not is_amd64():
        build_from_source()
    else:
        subprocess.check_call([PIP, "install", "ns3"])
import ns
