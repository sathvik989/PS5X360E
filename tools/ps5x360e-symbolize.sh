#!/usr/bin/env bash
# ps5x360e-symbolize.sh - turn the eboot+0x... places in a PS5 log into function names.
#
# Run from the workspace folder (the one with ps5x360e-build.sh), after the
# build that made the eboot.bin the log came from:
#     bash PS5X360E/tools/ps5x360e-symbolize.sh boot.log            (copied from the logs zip)
#     bash PS5X360E/tools/ps5x360e-symbolize.sh boot.log > places.txt
# Only the last session in the log is read (from the last "BOOT main entered").
# The names come from PS5X360E/build/canary-game/llvm-pie.elf, so a log from an
# older eboot.bin gives wrong names.
set -euo pipefail
LOG=${1:?usage: ps5x360e-symbolize.sh boot.log}
WS="$(pwd)"
ELF=PS5X360E/build/canary-game/llvm-pie.elf
[[ -f $ELF ]] || { echo "No $ELF here: run this from the workspace folder after a build."; exit 1; }
[[ -f $LOG ]] || { echo "No such log: $LOG"; exit 1; }
sudo docker run --rm -i -v "$WS":/ws -w /ws --user "$(id -u):$(id -g)" \
	-e LOG="$LOG" -e ELF="$ELF" ps5x360e-builder bash -s <<'INNER'
set -euo pipefail
python3 - "$LOG" "$ELF" <<'PY'
import re, subprocess, sys
log, elf = sys.argv[1], sys.argv[2]
lines = open(log, errors="replace").read().split("\n")
start = max((i for i, l in enumerate(lines) if "BOOT main entered" in l), default=0)
session = lines[start:]
offsets = sorted({int(m, 16) for l in session for m in re.findall(r"eboot\+0x([0-9a-fA-F]+)", l)})
if not offsets:
    sys.exit("No eboot+0x... places in the last session of " + log)
out = subprocess.run(["llvm-symbolizer-18", "--obj=" + elf, "--demangle", "--functions=linkage",
                      "--inlines", "--output-style=LLVM"],
                     input="\n".join(hex(o) for o in offsets), capture_output=True, text=True).stdout
blocks = out.strip("\n").split("\n\n") if "\n\n" in out else None
names = {}
# LLVM style: "function\nfile:line:col" per frame (inlined frames first), a blank line after each address.
res = out.split("\n")
i = 0
for o in offsets:
    frames = []
    while i + 1 < len(res) and res[i] != "":
        frames.append(f"{res[i]} ({res[i+1].rsplit('/', 1)[-1]})")
        i += 2
    i += 1
    names[o] = " <- ".join(frames) if frames else "?"
print("== places")
for o in offsets:
    print(f"eboot+0x{o:x}: {names[o]}")
print("\n== threads (WHERE lines of the last session, top of stack first)")
for l in session:
    m = re.search(r"WHERE (\S+) rip=(\S+)", l)
    if not m:
        continue
    stack = [names.get(int(x, 16), "?").split(" <- ")[-1].split(" (")[0]
             for x in re.findall(r"eboot\+0x([0-9a-fA-F]+)", l)]
    print(f"{m.group(1)} rip={m.group(2)}: " + " | ".join(stack[:10]))
PY
INNER
