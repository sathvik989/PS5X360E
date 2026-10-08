#!/usr/bin/env bash
# ps5x360e-build.sh - set up and build PS5X360E on the TrueNAS, inside Docker.
#
# Run it from the empty workspace folder (e.g. .../githubcomp/ps5x360):
#     bash ps5x360e-build.sh            set up everything that is missing, then build
#     bash ps5x360e-build.sh --image    rebuild the Docker builder image first
#
# It builds the branch named in BRANCH below from github.com/sathvik989/PS5X360E,
# pulling the latest commits of it on every run.
#
# Everything is written inside this folder:
#     PS5X360E/                              your fork (github.com/sathvik989/PS5X360E)
#     PS5X360E/.deps/references/PS5_Vulkan   mihawk-99's PS5 Vulkan tree (built RADV, SDK, zlib)
#     PS5X360E/.deps/references/PS5_Mesa     the Mesa fork RADV is built from
#     PS5X360E/.deps/references/PS5_PayloadSDK  the payload SDK fork
#     Castation/native-ps5/                  BrinooTk's Castation (only its image checker is used)
#     build-YYYYMMDD-HHMMSS.log              full output of this run - send it if anything fails
#
# Re-running is safe: finished steps are skipped (RADV and the SDK record what they
# were built from), so after the first build only changed code is recompiled.
set -euo pipefail

WS="$(pwd)"
# The tag changes whenever the image gains something a build needs, so an old
# image is replaced without --image.
IMAGE="ps5x360e-builder:edge1"
BRANCH="edge-core"
LOG="$WS/build-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG") 2>&1
echo "== PS5X360E build, $(date), workspace: $WS"

if [[ "${1:-}" == "--image" ]]; then
	sudo docker rmi -f "$IMAGE" >/dev/null 2>&1 || true
fi

if ! sudo docker image inspect "$IMAGE" >/dev/null 2>&1; then
	echo "== Building the Docker image $IMAGE (one time, several minutes)"
	sudo docker build -t "$IMAGE" - <<'EOF'
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates git wget curl unzip tar xz-utils zstd rsync make cmake ninja-build pkg-config file xxd \
      build-essential python3 python3-pip python3-venv python3-mako python3-yaml python3-ply python3-packaging \
      bison flex glslang-tools spirv-tools zlib1g-dev libelf-dev \
      clang-18 lld-18 llvm-18 llvm-18-dev llvm-18-tools libclang-18-dev libclang-cpp18-dev libclang-rt-18-dev \
      libclc-18 libclc-18-dev llvm-spirv-18 libllvmspirvlib-18-dev \
 && rm -rf /var/lib/apt/lists/* \
 && pip3 install --break-system-packages "meson>=1.4" \
 && for t in clang clang++ llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-config lld ld.lld; do \
      [ -x /usr/bin/$t-18 ] && ln -sfn /usr/bin/$t-18 /usr/local/bin/$t || true; done
# Xenia Canary's shader build runs "spirv-opt --canonicalize-ids", which only exists
# in SPIRV-Tools v2025.3 and newer; Ubuntu 24.04 ships a 2023 version. Build v2025.4
# into /opt/spirv-tools and put just its programs first on PATH (libraries untouched).
RUN git clone -q --depth 1 --branch v2025.4 https://github.com/KhronosGroup/SPIRV-Tools /tmp/st \
 && git clone -q https://github.com/KhronosGroup/SPIRV-Headers /tmp/st/external/spirv-headers \
 && git -C /tmp/st/external/spirv-headers checkout -q 01e0577914a75a2569c846778c2f93aa8e6feddd \
 && cmake -S /tmp/st -B /tmp/st/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSPIRV_SKIP_TESTS=ON -DSPIRV_WERROR=OFF -DCMAKE_INSTALL_PREFIX=/opt/spirv-tools \
 && ninja -C /tmp/st/build install \
 && for t in /opt/spirv-tools/bin/spirv-*; do ln -sfn "$t" /usr/local/bin/; done \
 && rm -rf /tmp/st \
 && spirv-opt --version && spirv-opt --help | grep -q -- --canonicalize-ids
# Xenia Edge compiles its shaders with Slang; xenia-build.py pins 2026.8.
RUN mkdir -p /opt/slang \
 && wget -q -O /tmp/slang.tar.gz https://github.com/shader-slang/slang/releases/download/v2026.8/slang-2026.8-linux-x86_64.tar.gz \
 && tar -xzf /tmp/slang.tar.gz -C /opt/slang && rm /tmp/slang.tar.gz \
 && /opt/slang/bin/slangc -v
ENV SLANGC_PATH=/opt/slang/bin/slangc
EOF
fi

echo "== Running the build inside Docker (as $(id -u):$(id -g))"
sudo docker run --rm -i --network host --dns 1.1.1.1 \
	-v "$WS":/ws -w /ws -e HOME=/ws/.home -e BRANCH="$BRANCH" --user "$(id -u):$(id -g)" \
	"$IMAGE" bash -s <<'INNER'
set -euo pipefail
mkdir -p "$HOME"
git config --global --add safe.directory '*'
git config --global advice.detachedHead false

R=/ws/PS5X360E
BRANCH=${BRANCH:-edge-core}
REFS=$R/.deps/references

# Clone a repository and check out an exact commit (shallow, idempotent).
clone_at() {
	local url=$1 dir=$2 rev=$3
	if [[ ! -d $dir/.git ]]; then
		mkdir -p "$dir"
		git -C "$dir" init -q
		git -C "$dir" remote add origin "$url"
	fi
	if ! git -C "$dir" cat-file -e "$rev^{commit}" 2>/dev/null; then
		git -C "$dir" fetch -q --depth 1 origin "$rev"
	fi
	git -C "$dir" checkout -q --detach "$rev"
	echo "   $(basename "$dir") at ${rev:0:12}"
}

echo "== 1/7 Your fork, branch $BRANCH (latest)"
if [[ ! -d $R/.git ]]; then
	git clone -q https://github.com/sathvik989/PS5X360E "$R"
fi
git -C "$R" fetch -q origin
git -C "$R" checkout -q -f -B "$BRANCH" "origin/$BRANCH"
echo "   PS5X360E at $(git -C "$R" rev-parse --short HEAD): $(git -C "$R" log -1 --format=%s)"

echo "== 2/7 Pinned dependencies (versions from PS5X360's docs/CREDITS.md and PS5_Vulkan's scripts)"
clone_at https://github.com/mihawk-99/PS5_Vulkan     "$REFS/PS5_Vulkan"     3f3ee69607013b345d2baa6d6a37c86745649a08
clone_at https://github.com/mihawk-99/PS5_Mesa       "$REFS/PS5_Mesa"       0b2d6d1a61d9bbf89cf8beb88a696144f67c61f8
clone_at https://github.com/mihawk-99/PS5_PayloadSDK "$REFS/PS5_PayloadSDK" 95c08f27386fc698f6bbe21dde3030140a41d10b
# Castation's pinned commit (94dfef7) was never published; only tools/verify-image.py
# is needed from it, and the public version has it.
if [[ ! -d /ws/Castation/native-ps5/.git ]]; then
	git clone -q --depth 1 https://github.com/BrinooTk/castation /ws/Castation/native-ps5
fi
echo "   castation at $(git -C /ws/Castation/native-ps5 rev-parse --short HEAD) (public head; see note above)"

echo "== 3/7 PS5 payload SDK and zlib (PS5_Vulkan tools/setup-native-dependencies.sh)"
( cd "$REFS/PS5_Vulkan" && bash tools/setup-native-dependencies.sh )

echo "== 4/7 RADV release build (PS5_Vulkan tools/build-radv.sh release) - the slow step, first time only"
( cd "$REFS/PS5_Vulkan" && bash tools/build-radv.sh release )

echo "== 5/7 Xenia Edge at the pinned commit + the PS5 patch + PS5X360E's patches"
mkdir -p "$R/build/native-runtime-stage/tools"
cp /ws/Castation/native-ps5/tools/verify-image.py "$R/build/native-runtime-stage/tools/verify-image.py"
# PS5X360's prepare_canary.py expects Canary already checked out: on a fresh
# --no-checkout clone it sees every file as deleted and fails restoring them.
# Check the pinned revision out first (once); after that it works as designed.
C=$R/.deps/xenia-canary
CANARY_URL=$(python3 -c "import json;print(json.load(open('$R/deps.json'))['xenia_canary']['url'])")
CANARY_REV=$(python3 -c "import json;print(json.load(open('$R/deps.json'))['xenia_canary']['revision'])")
# The core moved from Xenia Canary to Xenia Edge: a tree cloned from another
# repository is replaced (it is only a download; nothing of yours is in it).
if [[ -d $C/.git && $(git -C "$C" remote get-url origin) != "$CANARY_URL" ]]; then
	echo "   replacing $(git -C "$C" remote get-url origin) with $CANARY_URL"
	rm -rf "$C"
fi
if [[ ! -d $C/.git ]]; then
	git clone -q --filter=blob:none --no-checkout "$CANARY_URL" "$C"
fi
gitc() { git -C "$C" -c core.autocrlf=false -c core.safecrlf=false "$@"; }
# What the tree should be made of: the pinned revision, PS5X360's patch and the
# fork's own patches. When that is what it already is, its files (and their
# times) are left alone, so the build only recompiles what changed.
STAMP=$C/.git/ps5x360e-patches
WANT=$( { echo "$CANARY_REV"; cat "$R/patches/canary/xbox360ps5.patch" "$R"/patches/ps5x360e/*.patch 2>/dev/null || true; } | sha256sum | cut -c1-64)
if [[ -n $(gitc ls-files | head -1) && -f $STAMP && $(cat "$STAMP") == "$WANT" ]]; then
	echo "   Xenia Canary ${CANARY_REV:0:12} already has the current patches"
elif [[ -n $(gitc ls-files | head -1) && -f $STAMP && -f $R/tools/ps5x360e-sync-canary.py ]]; then
	# Prepared before with other patches: rewrite only the files whose content
	# changes, so only what the patch change touches is recompiled.
	gitc cat-file -e "$CANARY_REV^{commit}" 2>/dev/null || gitc fetch -q origin "$CANARY_REV"
	rm -f "$STAMP"
	( cd "$R" && python3 tools/ps5x360e-sync-canary.py )
	echo "$WANT" > "$STAMP"
else
	# First time: prepare_canary.py fails on a re-run, because files a patch
	# adds are left behind; start from a clean checkout of the pinned revision.
	gitc cat-file -e "$CANARY_REV^{commit}" 2>/dev/null || gitc fetch -q origin "$CANARY_REV"
	echo "   resetting Xenia Canary to ${CANARY_REV:0:12} and applying the patches"
	rm -f "$STAMP"
	gitc checkout -q -f --detach "$CANARY_REV"
	gitc clean -fdq
	( cd "$R" && python3 tools/prepare_canary.py )
	echo "$WANT" > "$STAMP"
fi

echo "== 6/7 Xenia Edge's shader compiler for this machine (first time several minutes)"
H=$R/build/host-shader-cc
if [[ ! -f $H/build.ninja ]]; then
	cmake -S "$R/tools/host-shader-cc" -B "$H" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DXENIA_SOURCE="$C" >/dev/null
fi
ninja -C "$H" -j"$(nproc)" xenia-shader-cc | tail -1

echo "== 7/7 Building the PS5 title"
( cd "$R" && PS5_PAYLOAD_SDK="$REFS/PS5_Vulkan/.deps/native/ps5-payload-sdk" JOBS="$(nproc)" \
	XE_HOST_SHADER_CC="$H/xenia-shader-cc" SLANGC_PATH="$SLANGC_PATH" \
	bash tools/build-canary-game.sh )

echo
echo "== DONE"
ls -l "$R/build/canary-game/eboot.bin"
echo "   built from PS5X360E $(git -C "$R" rev-parse --short HEAD), $(date)"
INNER

echo
echo "Next: on the PS5, close PS5X360, then replace /data/homebrew/PPSA50011/eboot.bin"
echo "with PS5X360E/build/canary-game/eboot.bin (keep a copy of the original first)."
echo "Full log: $LOG"
