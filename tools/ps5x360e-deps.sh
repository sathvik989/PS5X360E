#!/usr/bin/env bash
# ps5x360e-deps.sh - the pinned PS5 dependencies PS5X360E builds with.
#   bash tools/ps5x360e-deps.sh            check out the pins, set up the SDK, build RADV
#   bash tools/ps5x360e-deps.sh --check    only verify the checkouts match the pins
#
# Run by tools/ps5x360e-build.sh (since alpha.48), so a pin change in the fork
# reaches the build without a new copy of that script. The pins follow
# PS5_Vulkan: its tools/build-radv.sh names the PS5_Mesa revision and
# tools/setup-native-dependencies.sh the PS5_PayloadSDK one.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
refs="$root/.deps/references"

PS5_VULKAN_REV=252bf912a745e3566a93b69b098bdc8ec4048837
PS5_MESA_REV=47e9eb61ee7e1112194557e1e047178562827bc6
PS5_PAYLOAD_SDK_REV=b83202be73e930050e00de0f1b4d9ec46c0391bf

pins=(
	"https://github.com/mihawk-99/PS5_Vulkan     PS5_Vulkan     $PS5_VULKAN_REV"
	"https://github.com/mihawk-99/PS5_Mesa       PS5_Mesa       $PS5_MESA_REV"
	"https://github.com/mihawk-99/PS5_PayloadSDK PS5_PayloadSDK $PS5_PAYLOAD_SDK_REV"
)

if [[ "${1:-}" == "--check" ]]; then
	bad=0
	for pin in "${pins[@]}"; do
		read -r _ name rev <<<"$pin"
		have=$(git -C "$refs/$name" rev-parse HEAD 2>/dev/null || echo none)
		if [[ $have != "$rev" ]]; then
			echo "   $name is at ${have:0:12}, this branch builds with ${rev:0:12}" >&2
			bad=1
		fi
	done
	exit $bad
fi

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

for pin in "${pins[@]}"; do
	read -r url name rev <<<"$pin"
	clone_at "$url" "$refs/$name" "$rev"
done

echo "== PS5 payload SDK and zlib (PS5_Vulkan tools/setup-native-dependencies.sh)"
( cd "$refs/PS5_Vulkan" && bash tools/setup-native-dependencies.sh )

echo "== RADV release build (PS5_Vulkan tools/build-radv.sh release) - slow when the Mesa pin changes"
( cd "$refs/PS5_Vulkan" && bash tools/build-radv.sh release )
