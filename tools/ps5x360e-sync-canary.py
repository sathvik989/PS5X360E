"""Bring an already prepared Xenia Canary tree to the pinned revision plus the
current patches (PS5X360's, then patches/ps5x360e/*.patch), rewriting only the
files whose content changes, so the next build recompiles only what the patch
change touches.

    python3 tools/ps5x360e-sync-canary.py

The wanted tree is built in a separate git index (git apply --cached), so the
working files are not touched to get there; then every file that differs from
the pinned revision, before or after, is compared with what it should be.
Untracked files no patch creates any more are left alone. For a tree that was
never prepared, use tools/prepare_canary.py (it also fetches the submodules).
"""
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / ".deps/xenia-canary"
PATCHES = [ROOT / "patches/canary/xbox360ps5.patch", *sorted((ROOT / "patches/ps5x360e").glob("*.patch"))]


def git(*args, index=None, data=False):
    env = dict(os.environ)
    if index:
        env["GIT_INDEX_FILE"] = str(index)
    out = subprocess.run(["git", "-C", str(SOURCE), "-c", "core.autocrlf=false", "-c", "core.safecrlf=false", *args],
                         check=True, capture_output=True, env=env).stdout
    return out if data else out.decode()


def names(text):
    return [n for n in text.split("\0") if n]


def main():
    revision = json.loads((ROOT / "deps.json").read_text())["xenia_canary"]["revision"]
    index = SOURCE / ".git/ps5x360e-index"
    index.unlink(missing_ok=True)
    git("read-tree", revision, index=index)
    for patch in PATCHES:
        try:
            git("apply", "--cached", "--whitespace=nowarn", str(patch), index=index)
        except subprocess.CalledProcessError as error:
            sys.exit(f"{patch.name} does not apply: {error.stderr.decode().strip()}")
    wanted = git("write-tree", index=index).strip()
    index.unlink(missing_ok=True)

    paths = set(names(git("diff", "--no-renames", "--name-only", "-z", revision, wanted)))
    paths |= set(names(git("diff", "--no-renames", "--name-only", "-z", revision)))
    entries = {}
    for line in git("ls-tree", "-r", "-z", wanted, "--", *sorted(paths)).split("\0") if paths else []:
        if line:
            meta, path = line.split("\t", 1)
            mode, _, blob = meta.split()
            entries[path] = (mode, blob)
    written = removed = 0
    for path in sorted(paths):
        target = SOURCE / path
        if path not in entries:
            if target.is_file():
                target.unlink()
                removed += 1
            continue
        mode, blob = entries[path]
        content = git("cat-file", "blob", blob, data=True)
        if target.is_file() and target.read_bytes() == content:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        os.chmod(target, 0o755 if mode == "100755" else 0o644)
        written += 1
    print(f"Xenia Canary {revision[:8]} with {len(PATCHES)} patches: {written} files rewritten, "
          f"{removed} removed, {len(paths) - written - removed} unchanged")


if __name__ == "__main__":
    main()
