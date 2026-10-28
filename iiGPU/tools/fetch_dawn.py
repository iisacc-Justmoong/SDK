#!/usr/bin/env python3
"""Fetch a hash-locked, target-specific Dawn package beneath this SDK's resources/."""
import argparse
import base64
import hashlib
import json
from pathlib import Path, PurePosixPath
import platform
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def host_key(system=None, machine=None):
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    cpu = {"aarch64": "arm64", "arm64": "arm64", "amd64": "x64", "x86_64": "x64"}.get(machine)
    os_name = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}.get(system)
    if not cpu or not os_name:
        raise ValueError("No pinned Dawn binary for this host; provide a target-native IIGPU_DAWN_ROOT")
    return os_name + "-" + cpu


def default_destination(key):
    return ROOT / "resources/dawn" / key


def put_verified(path, data, *, storage):
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError("Existing dependency file differs; preserving it: " + str(path))
        return
    storage.mkdir(path.parent, parents=True, exist_ok=True)
    with storage.staged_path(path, overwrite=False) as temporary:
        with storage.open(temporary, "wb") as stream:
            stream.write(data)


def extract(archive, destination, *, storage):
    """Strip one archive root; preserve only the relocatable C/C++ distribution."""
    hashes = {}
    with tarfile.open(archive) as source:
        members = source.getmembers()
        prefix = None
        for member in members:
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or "\\" in member.name or member.issym() or member.islnk():
                raise ValueError("Unsafe archive member: " + member.name)
            if not path.parts:
                continue
            if prefix is None:
                prefix = path.parts[0]
            if path.parts[0] != prefix:
                raise ValueError("Dawn archive must have one root directory")
        for member in members:
            parts = PurePosixPath(member.name).parts[1:]
            if not parts or not member.isfile():
                continue
            if parts[0] not in ("lib", "lib64") and not (len(parts)>1 and parts[:2] in (
                    ("include", "dawn"), ("include", "webgpu"), ("include", "webgpu_upstream"))):
                continue
            data = source.extractfile(member).read()
            relative = PurePosixPath(*parts).as_posix()
            put_verified(destination.joinpath(*parts), data, storage=storage)
            hashes[relative] = digest(data)
    if "include/webgpu/webgpu.h" not in hashes or "lib/cmake/Dawn/DawnConfig.cmake" not in hashes:
        raise ValueError("Archive is not a complete relocatable Dawn distribution")
    return hashes


def checked_download(url, sha256, destination=None, encoded=False, *, storage):
    if destination and destination.is_file():
        data = destination.read_bytes()
    else:
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if encoded:
            data = base64.b64decode(data, validate=False)
    if digest(data) != sha256:
        raise ValueError("SHA-256 mismatch: " + url)
    if destination:
        put_verified(destination, data, storage=storage)
    return data


def verify(destination, record):
    for relative, sha256 in record["files"].items():
        path = destination / relative
        if not path.is_file() or digest(path.read_bytes()) != sha256:
            raise ValueError("Dependency verification failed: " + str(path))


def main(storage, argv=None):
    lock_bytes = (ROOT / "tools/dawn.lock.json").read_bytes()
    lock = json.loads(lock_bytes)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=tuple(lock["assets"]))
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args(argv)
    key = args.platform or host_key()
    if key not in lock["assets"]:
        parser.error("No pinned binary for " + key + "; provide a target-native Dawn source build")
    destination = (args.destination or default_destination(key)).resolve()
    if not destination.is_relative_to((ROOT / "resources").resolve()):
        parser.error("Dependency output must be inside iiGPU/resources/")
    asset = lock["assets"][key]
    marker = destination / "iiwebgpu-source.json"
    if marker.exists():
        record = json.loads(marker.read_text())
        if record["archive_sha256"] != asset["sha256"] or record["platform"] != key:
            raise ValueError("Existing Dawn target/revision differs; preserving it")
        if record.get("lock_sha256") == digest(lock_bytes):
            verify(destination, record)
            print("Verified Dawn " + lock["release"] + " for " + key + ": " + str(destination))
            return
    archive = ROOT / "resources/dawn-cache" / ("dawn-" + key + ".tar.gz")
    checked_download(asset["url"], asset["sha256"], archive, storage=storage)
    hashes = extract(archive, destination, storage=storage)
    for license in lock["licenses"]:
        path = destination / "licenses" / license["name"]
        data = checked_download(license["url"], license["sha256"], path, license["base64"], storage=storage)
        hashes[path.relative_to(destination).as_posix()] = digest(data)
    record = {"release": lock["release"], "revision": lock["revision"], "platform": key,
              "archive_sha256": asset["sha256"], "lock_sha256": digest(lock_bytes), "files": hashes}
    storage.write_text(marker, json.dumps(record, indent=2, sort_keys=True) + "\n")
    print("Verified Dawn " + lock["release"] + " for " + key + ": " + str(destination))


if __name__ == "__main__":
    raise SystemExit("Supply a storage adapter from an application; see SDK/tools/fetch_gpu_dawn.py")
