#!/usr/bin/env python3
"""Record post-merge firmware/libraries in a separate Git branch of the SAME repo.

This avoids writing the current HEAD to a file inside that very commit (a
self-reference), while retaining every historical revision in Git.
"""
import base64
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

AUDIT_BRANCH = "gpm-release-locks"
MANIFEST = Path(".gpm/avr-libraries.json")


def git(*args, cwd=None, check=True):
    return subprocess.run(["git", *map(str, args)], cwd=cwd, check=check,
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def record(branch, expected_head):
    if not (branch in ("main", "master") or re.fullmatch(r"release/.+", branch)):
        raise ValueError("Only release/* and main/master can create revision locks")
    head = git("rev-parse", "HEAD").stdout.strip()
    if not re.fullmatch("[a-f0-9]{40}", head) or head != expected_head:
        raise ValueError("Push SHA does not match checked out firmware commit")
    if not MANIFEST.is_file():
        raise ValueError("Missing versioned GPM library manifest")
    raw = MANIFEST.read_bytes()
    manifest = json.loads(raw)
    if manifest.get("schemaVersion") != 1:
        raise ValueError("Unsupported AVR dependency manifest schema")
    libraries = []
    for lib in manifest.get("gitRepositories", []):
        sha = lib.get("ref", "")
        if not isinstance(sha, str) or not re.fullmatch("[a-fA-F0-9]{40}", sha):
            raise ValueError("Git library must be pinned to a full SHA")
        libraries.append({"url": lib["url"], "sha": sha.lower()})
    data = {"schemaVersion": 1, "firmwareBranch": branch,
            "firmwareCommit": head, "manifestBlob": git("rev-parse", "HEAD:.gpm/avr-libraries.json").stdout.strip(),
            "gitLibraries": libraries}
    key = base64.urlsafe_b64encode(branch.encode()).decode().rstrip("=")
    relative = Path(".gpm") / "release-locks" / (key + ".json")
    exists = git("ls-remote", "--exit-code", "--heads", "origin",
                 "refs/heads/" + AUDIT_BRANCH, check=False).returncode == 0
    if exists:
        git("fetch", "--no-tags", "origin",
            "refs/heads/" + AUDIT_BRANCH + ":refs/remotes/origin/" + AUDIT_BRANCH)
    with tempfile.TemporaryDirectory(prefix="gpm-release-lock-") as scratch:
        worktree = Path(scratch) / "audit"
        base = "refs/remotes/origin/" + AUDIT_BRANCH if exists else "HEAD"
        git("worktree", "add", "--detach", str(worktree), base)
        try:
            if not exists:
                git("switch", "--orphan", "gpm-release-audit", cwd=worktree)
            target = worktree / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
            git("add", "--", str(relative), cwd=worktree)
            change = git("diff", "--cached", "--quiet", cwd=worktree, check=False).returncode
            if change == 0:
                print("GPM revision mapping unchanged:", branch, head)
                return
            if change != 1:
                raise RuntimeError("Cannot inspect release mapping changes")
            git("config", "user.name", "github-actions[bot]", cwd=worktree)
            git("config", "user.email",
                "41898282+github-actions[bot]@users.noreply.github.com", cwd=worktree)
            git("commit", "-m", "GPM: record firmware and libraries for " + branch, cwd=worktree)
            git("push", "origin", "HEAD:refs/heads/" + AUDIT_BRANCH, cwd=worktree)
            print("Saved GPM revision mapping:", branch, head, str(relative))
        finally:
            git("worktree", "remove", "--force", str(worktree))


if __name__ == "__main__":
    try:
        if len(sys.argv) != 3:
            raise ValueError("Usage: release-lock.py BRANCH GITHUB_SHA")
        record(sys.argv[1], sys.argv[2])
    except (ValueError, OSError, subprocess.CalledProcessError, RuntimeError) as exc:
        print("GPM release lock error:", exc, file=sys.stderr)
        sys.exit(1)
