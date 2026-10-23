#!/usr/bin/env bash
# Build macOS packages for both CPUs on a Mac. Each target needs the SDK for
# that CPU (it carries libRack.dylib for one arch), so they are fetched on
# first use into $SDK_ROOT/Rack-SDK-<version>-mac-<arch>.
# Output: build/mac/*.vcvplugin. Usage: tools/mac_dist.sh [arm64] [x64]
set -euo pipefail
cd "$(dirname "$0")/.."
[ "$(uname -s)" = Darwin ] || { echo "macOS only; Linux/Windows use tools/cross_build.sh"; exit 1; }

version="${RACK_SDK_VERSION:-2.6.6}"
sdk_root="${SDK_ROOT:-$HOME/ws/sdk}"
archs="${*:-arm64 x64}"
out=$(mktemp -d)

for arch in $archs; do
	sdk="$sdk_root/Rack-SDK-$version-mac-$arch"
	if [ ! -f "$sdk/plugin.mk" ]; then
		echo "fetching Rack SDK $version for mac-$arch"
		tmp=$(mktemp -d)
		curl -sSfL -o "$tmp/sdk.zip" "https://vcvrack.com/downloads/Rack-SDK-$version-mac-$arch.zip"
		unzip -q "$tmp/sdk.zip" -d "$tmp"
		mkdir -p "$sdk_root" && mv "$tmp/Rack-SDK" "$sdk" && rm -rf "$tmp"
	fi
	case $arch in
		arm64) target=arm64-apple-darwin ;;
		x64) target=x86_64-apple-darwin ;;
		*) echo "unknown arch $arch"; exit 1 ;;
	esac
	# `make clean` between arches, or objects from the other CPU get linked.
	make clean RACK_DIR="$sdk" >/dev/null
	make -j"$(sysctl -n hw.ncpu)" dist RACK_DIR="$sdk" CROSS_COMPILE=$target
	cp dist/*.vcvplugin "$out/"
done

# Copy out last: `make clean` deletes build/.
mkdir -p build/mac && mv "$out"/*.vcvplugin build/mac/ && rm -rf "$out"
ls -la build/mac
