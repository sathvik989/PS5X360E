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
	-e LOG="$LOG" -e ELF="$ELF" ps5x360e-builder:edge1 bash -s <<'INNER'
set -euo pipefail
python3 - "$LOG" "$ELF" <<'PY'
import re, subprocess, sys
log, elf = sys.argv[1], sys.argv[2]
lines = open(log, errors="replace").read().split("\n")
start = max((i for i, l in enumerate(lines) if "BOOT main entered" in l), default=0)
session = lines[start:]
# The guide's "Measure performance" lines (game log): eboot offsets in their
# title and system-from parts, written as hex keys before ':' or '<'.
def measure_parts(l):
    for part in l.split(" | "):
        name, _, rest = part.partition(" ")
        if name in ("title", "system-from"):
            yield name, rest
measure = [l for l in session if "MEASURE thread" in l or "MEASURE focused" in l]
measured = {int(k, 16) for l in measure for _, rest in measure_parts(l)
            for k in re.findall(r"([0-9A-F]+)(?=[:<])|(?<=<)([0-9A-F]+)", rest) for k in k if k}
measured.discard(0)
offsets = sorted({int(m, 16) for l in session for m in re.findall(r"eboot\+0x([0-9a-fA-F]+)", l)} | measured)
if not offsets:
    sys.exit("No eboot+0x... places or MEASURE lines in the last session of " + log)
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
if measure:
    print("\n== measure (eboot offsets named; percentages as in the log)")
    short = lambda o: names.get(o, "?").split(" <- ")[-1].split(" (")[0]
    for l in measure:
        head = l[l.index("MEASURE"):].split(" | ")[0]
        print(head)
        for name, rest in measure_parts(l):
            for item in rest.split():
                key, _, share = item.rpartition(":")
                at, _, caller = key.partition("<")
                text = short(int(at, 16)) if at else "?"
                if caller:
                    text += "  called from  " + short(int(caller, 16))
                print(f"    {name:12} {share.rstrip('%') + '%':>6}  {text}")
print("\n== threads (WHERE lines of the last session, top of stack first)")
for l in session:
    m = re.search(r"WHERE (\S+) rip=(\S+)", l)
    if not m:
        continue
    stack = [names.get(int(x, 16), "?").split(" <- ")[-1].split(" (")[0]
             for x in re.findall(r"eboot\+0x([0-9a-fA-F]+)", l)]
    print(f"{m.group(1)} rip={m.group(2)}: " + " | ".join(stack[:10]))
# The machine code at a crash in the eboot, with source lines.
for l in session:
    m = re.search(r"CRASH .*rip=eboot\+0x([0-9a-fA-F]+)", l)
    if not m:
        continue
    at = int(m.group(1), 16)
    print(f"\n== code at the crash, eboot+0x{at:x}")
    dis = subprocess.run(["llvm-objdump-18", "-d", "-l", "-C", "--no-show-raw-insn",
                          f"--start-address={hex(max(at - 0xc0, 0))}", f"--stop-address={hex(at + 0x20)}", elf],
                         capture_output=True, text=True).stdout
    print("\n".join(dis.splitlines()[4:]))
PY
INNER
