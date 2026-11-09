#!/usr/bin/env python3
"""Build Arduino firmware using portable project library dependencies."""
import json
import hashlib
import os
import re
import pathlib
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit


def run(*args):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run([str(arg) for arg in args], check=True)


def project_directory(value):
    if not isinstance(value, str) or not value or pathlib.PurePath(value).is_absolute():
        raise ValueError("Library path must be relative to the repository")
    root = pathlib.Path.cwd().resolve()
    path = (root / value).resolve()
    if not path.is_relative_to(root) or not path.is_dir():
        raise ValueError("Invalid or missing project library folder: " + value)
    return path


def add_library_path(arguments, path, layout):
    if layout not in ("collection", "library"):
        raise ValueError("Library layout must be 'collection' or 'library'")
    arguments.extend(["--libraries" if layout == "collection" else "--library", str(path)])


def is_official_library_checkout(directory):
    """Pinned detached commits are official when reachable from main/master."""
    containing = subprocess.check_output(
        ["git", "-C", str(directory), "branch", "--all", "--contains",
         "HEAD", "--format=%(refname:short)"],
        text=True,
    )
    official = {"main", "master", "origin/main", "origin/master"}
    return any(name.strip() in official for name in containing.splitlines())


def main():
    if len(sys.argv) != 3:
        raise ValueError("Usage: avr-ci.py FQBN SKETCH_DIRECTORY")

    manifest_path = pathlib.Path(".gpm/avr-libraries.json")
    if not manifest_path.is_file():
        raise ValueError("GPM library manifest not found: " + str(manifest_path))
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    if data.get("schemaVersion") != 1:
        raise ValueError("Unsupported GPM library manifest version")

    # The manifest is committed alongside the firmware: this is the exact
    # dependency revision transported through feature/develop/release PRs.
    versions = []
    git_refs = []
    summary_lines = ["### AVR firmware dependency versions", "",
                     "| Source | Pinned version |", "| --- | --- |"]
    for source in data.get("gitRepositories", []):
        url, ref = source["url"], source.get("ref", "")
        if not re.fullmatch(r"[0-9a-fA-F]{40}", ref):
            raise ValueError("Git library must be pinned to a full commit SHA: " + url)
        versions.append(url + "@" + ref)
        git_refs.append(ref)
        summary_lines.append("| `" + url + "` | `" + ref + "` |")
        print("CI Git library:", url, "@", ref, flush=True)
    for library in data.get("arduinoLibraryManager", []):
        name, version = library["name"], library.get("version", "")
        if not version:
            raise ValueError("Unpinned Library Manager version: " + name)
        versions.append(name + "@" + version)
        summary_lines.append("| `" + name + "` | `" + version + "` |")
    for local in data.get("projectLibraryPaths", []):
        summary_lines.append("| `" + local["path"] + "` | In firmware commit |")
    summary_file = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_file:
        with open(summary_file, "a", encoding="utf-8") as summary:
            summary.write("\n".join(summary_lines) + "\n")
    header = pathlib.Path(sys.argv[2]) / "GitVersion.generated.h"
    if not header.is_file():
        raise ValueError("Missing generated firmware version header: " + str(header))
    # One Git library uses its abbreviated commit, just like GIT_VERSION.
    # For multiple pinned dependencies, use a stable 7-character fingerprint.
    if len(versions) == 1 and len(git_refs) == 1:
        short_version = git_refs[0][:7].lower()
    else:
        # Deterministic fingerprint for multiple (or zero) dependencies.
        short_version = hashlib.sha256(";".join(versions).encode("utf-8")).hexdigest()[:7]
    # Library provenance is independent of the firmware CI source branch.
    has_unofficial_git_libraries = False
    build_args = ["arduino-cli", "compile", "--fqbn", sys.argv[1]]
    with tempfile.TemporaryDirectory(prefix="gpm-avr-libraries-") as tmp:
        for lib in data.get("arduinoLibraryManager", []):
            name = lib["name"]
            version = lib.get("version", "")
            if not isinstance(name, str) or not name.strip() or not isinstance(version, str):
                raise ValueError("Invalid Arduino Library Manager dependency")
            run("arduino-cli", "lib", "install", name + ("@" + version if version else ""))

        for index, source in enumerate(data.get("gitRepositories", [])):
            url = source["url"]
            parsed = urlsplit(url)
            if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
                raise ValueError("Git library source must be a credential-free HTTPS URL")
            destination = pathlib.Path(tmp) / ("source-" + str(index))
            run("git", "clone", "--quiet", "--", url, destination)
            ref = source.get("ref", "")
            if ref:
                if not isinstance(ref, str) or ref.startswith("-"):
                    raise ValueError("Invalid Git reference")
                run("git", "-C", destination, "checkout", "--quiet", "--detach", ref)
            if not is_official_library_checkout(destination):
                has_unofficial_git_libraries = True
            subdir = source.get("subdirectory", "")
            if not isinstance(subdir, str):
                raise ValueError("Invalid Git library subdirectory")
            library_root = (destination / subdir).resolve()
            if not library_root.is_relative_to(destination.resolve()) or not library_root.is_dir():
                raise ValueError("Git library subdirectory missing or outside repository: " + subdir)
            add_library_path(build_args, library_root, source.get("layout", "collection"))

        for local in data.get("projectLibraryPaths", []):
            path = project_directory(local["path"])
            add_library_path(build_args, path, local.get("layout", "collection"))

        # Any Git dependency absent from main/master marks the combined version.
        if has_unofficial_git_libraries:
            short_version += "*"
        with header.open("a", encoding="utf-8") as output:
            output.write("#define GIT_LIBRARIES_VERSION " + json.dumps(short_version) + "\n")
        print("CI GIT_LIBRARIES_VERSION:", short_version, flush=True)

        build_args.append(sys.argv[2])
        run(*build_args)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as exc:
        print("GPM AVR CI error:", exc, file=sys.stderr)
        sys.exit(1)
