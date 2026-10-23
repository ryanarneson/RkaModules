#!/usr/bin/env bash
# Build Windows and Linux packages locally with the Docker cross toolchain
# (the same image CI uses). Output: packages/*.vcvplugin, kept outside build/
# because `make clean` (run by the toolchain and mac_dist.sh) wipes build/.
# macOS packages need Apple's SDK and come from tools/mac_dist.sh or CI.
set -euo pipefail
cd "$(dirname "$0")/.."
image="${TOOLCHAIN_IMAGE:-ghcr.io/bloodbat/rack-plugin-toolchain-win-linux}"
platforms="${*:-win-x64 lin-x64}"
targets=$(for p in $platforms; do printf 'plugin-build-%s ' "$p"; done)
# linux/amd64: the image is x86 only; on Apple Silicon this runs under Rosetta.
docker run --rm --platform linux/amd64 -v "$PWD":/plugin -e PLUGIN_DIR=/plugin --user root "$image" bash -c "
	set -e
	cd /home/build/rack-plugin-toolchain
	make $targets
	mkdir -p /plugin/packages
	cp plugin-build/*.vcvplugin /plugin/packages/
	# The toolchain runs as root; hand everything back to the caller.
	rm -rf /plugin/dep /plugin/dist
	chown -R $(id -u):$(id -g) /plugin/packages
"
ls -la packages
