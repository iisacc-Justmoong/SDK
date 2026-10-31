#!/usr/bin/env python3
"""Clone or fast-forward the workspace from Git; never copy source directories."""
import argparse
import datetime
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys


def git(directory, *arguments):
    environment = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    command = ["git", "-c", "core.longpaths=true"]
    if directory is not None:
        command += ["-C", str(directory)]
    result = subprocess.run([*command, *arguments], capture_output=True, text=True,
                            encoding="utf-8", errors="replace", env=environment)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip()
                           or f"Git exited {result.returncode}")
    return result.stdout.strip()


def relative_path(value):
    if not isinstance(value, str):
        raise ValueError(f"Invalid workspace path: {value}")
    path = PurePosixPath(value)
    if ("\\" in value or path.is_absolute()
            or not path.parts or path.parts[0] not in {"SDK", "Product", "Service"}
            or any(part in {".", ".."} for part in value.split("/"))):
        raise ValueError(f"Invalid workspace path: {value}")
    for part in path.parts:
        if (re.search(r'[<>:"\\|?*\x00-\x1f]', part) or part.endswith((" ", "."))
                or re.match(r"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)", part, re.I)):
            raise ValueError(f"Invalid Windows path: {value}")
    return path


def destination(root, value):
    path = relative_path(value)
    target = root.joinpath(*path.parts)
    for item in [target, *target.parents]:
        if item == root.parent:
            break
        if item.is_symlink():
            raise ValueError(f"Symlink destination is not supported: {item}")
    if not target.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"Destination escapes workspace: {value}")
    return target


def canonical_url(value):
    # Preserve the Mac's local names while allowing existing SSH GitHub remotes.
    value = re.sub(r"^git@github\.com:", "https://github.com/", value)
    value = re.sub(r"^ssh://git@github\.com/", "https://github.com/", value)
    if value.startswith("https://github.com/"):
        return value.rstrip("/").removesuffix(".git").casefold()
    return value.rstrip("/").removesuffix(".git")


def load_manifest(path):
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("version") != 1 or not manifest.get("repositories"):
        raise ValueError("Expected workspace manifest version 1 with repositories")
    seen = set()
    for row in manifest["repositories"]:
        name = str(relative_path(row["path"]))
        if name.casefold() in seen:
            raise ValueError(f"Duplicate workspace path: {name}")
        seen.add(name.casefold())
        if not isinstance(row.get("url"), str) or row["url"].startswith("-"):
            raise ValueError(f"Invalid Git remote: {name}")
        branch = row.get("branch", "")
        if not branch or branch.startswith("-"):
            raise ValueError(f"Invalid Git branch: {name}")
        git(None, "check-ref-format", "refs/heads/" + branch)
        commit = row.get("sourceCommit")
        if commit is not None and not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise ValueError(f"Invalid source commit: {name}")
    for name in manifest.get("directories", []):
        relative_path(name)
    return manifest


def existing_container(target, root, known_directories):
    """Accept an existing container of known children, preserving every child."""
    for entry in target.iterdir():
        if not entry.is_dir() or entry.is_symlink():
            return False
        name = entry.relative_to(root).as_posix()
        if not any(path == name or path.startswith(name + "/") for path in known_directories):
            return False
    return True


def sync_repository(root, row, known_directories, audit_only=False):
    target = destination(root, row["path"])
    url, branch = row["url"], row["branch"]
    marker = target / ".git"
    if not marker.exists():
        if audit_only:
            raise RuntimeError("Checkout is missing")
        if row.get("lfs"):
            git(None, "lfs", "version")
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() and any(target.iterdir()):
            if not existing_container(target, root, known_directories):
                raise RuntimeError("Destination contains existing files without its own Git repository")
            git(target, "init", "-b", branch)
            git(target, "config", "core.autocrlf", "false")
            git(target, "remote", "add", "origin", url)
            git(target, "fetch", "--tags", "origin")
            git(target, "checkout", "--track", "-b", branch, "origin/" + branch)
        else:
            git(None, "clone", "--config", "core.autocrlf=false", "--config", "core.longpaths=true",
                "--branch", branch, "--", url, str(target))
    actual_root = Path(git(target, "rev-parse", "--show-toplevel")).resolve()
    if actual_root != target.resolve():
        raise RuntimeError("Destination is not an independent Git repository")
    if canonical_url(git(target, "remote", "get-url", "origin")) != canonical_url(url):
        raise RuntimeError("Existing origin differs from the workspace manifest")
    if git(target, "branch", "--show-current") != branch:
        raise RuntimeError(f"Existing branch differs from {branch}; local work was preserved")
    if git(target, "status", "--porcelain", "--untracked-files=all"):
        raise RuntimeError("Checkout has uncommitted changes; local work was preserved")
    if not audit_only:
        git(target, "config", "core.longpaths", "true")
        git(target, "fetch", "--prune", "--tags", "origin")
    reference = "refs/remotes/origin/" + branch
    remote_head = git(target, "ls-remote", "--exit-code", "origin", "refs/heads/" + branch).split()[0]
    if not audit_only and git(target, "rev-parse", reference) != remote_head:
        raise RuntimeError("Remote changed during fetch; rerun to synchronize the new commit")
    source = row.get("sourceCommit")
    if source:
        git(target, "merge-base", "--is-ancestor", source, remote_head)
    if not audit_only:
        git(target, "merge", "--ff-only", reference)
        git(target, "branch", "--set-upstream-to=origin/" + branch, branch)
        if row.get("lfs"):
            git(target, "lfs", "install", "--local")
            git(target, "lfs", "pull")
            git(target, "lfs", "fsck")
        if (target / ".gitmodules").exists():
            git(target, "submodule", "update", "--init", "--recursive")
    head = git(target, "rev-parse", "HEAD")
    if head != remote_head:
        raise RuntimeError(f"Local HEAD {head} differs from GitHub {remote_head}; local commits were preserved")
    if git(target, "status", "--porcelain", "--untracked-files=all"):
        raise RuntimeError("Checkout is not clean after synchronization")
    return {"path": row["path"], "directory": str(target), "branch": branch,
            "head": head, "remoteHead": remote_head, "verified": True,
            "snapshotExact": source is None or head == source}


def synchronize(root, manifest_path, audit_only=False):
    manifest = load_manifest(manifest_path)
    root = root.expanduser().absolute()
    if root.is_symlink():
        raise ValueError(f"Symlink workspace root is not supported: {root}")
    known = [r["path"] for r in manifest["repositories"]] + manifest.get("directories", [])
    rows = sorted(manifest["repositories"], key=lambda row: (len(PurePosixPath(row["path"]).parts), row["path"]))
    report = {"timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "root": str(root), "auditOnly": audit_only, "repositories": [], "directoryErrors": []}
    for row in rows:
        try:
            result = sync_repository(root, row, known, audit_only)
        except (OSError, ValueError, RuntimeError) as error:
            result = {"path": row["path"], "verified": False, "error": str(error)}
        report["repositories"].append(result)
        print(f"{'VERIFIED' if result['verified'] else 'FAILED'} {row['path']}"
              + ("" if result["verified"] else ": " + result["error"]), flush=True)
    for name in manifest.get("directories", []):
        try:
            target = destination(root, name)
            if audit_only:
                if not target.is_dir():
                    raise RuntimeError("Directory is missing")
            else:
                target.mkdir(parents=True, exist_ok=True)
        except (OSError, ValueError, RuntimeError) as error:
            report["directoryErrors"].append({"path": name, "error": str(error)})
    report["verified"] = sum(row["verified"] for row in report["repositories"])
    report["total"] = len(rows)
    report["complete"] = report["verified"] == report["total"] and not report["directoryErrors"]
    print(f"Verified {report['verified']}/{report['total']}", flush=True)
    print("WORKSPACE_SYNC_REPORT=" + json.dumps(report, ensure_ascii=False, separators=(',', ':')), flush=True)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.home() / "Workspace")
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("workspace-repositories.json"))
    parser.add_argument("--audit-only", action="store_true")
    args = parser.parse_args(argv)
    if not shutil.which("git"):
        parser.error("Git is required; install Git for Windows and authenticate to GitHub")
    try:
        return 0 if synchronize(args.root, args.manifest, args.audit_only)["complete"] else 1
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
