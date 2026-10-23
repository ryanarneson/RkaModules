#!/usr/bin/env bash
# Build Windows and Linux packages locally with the Docker cross toolchain
# (the same image CI uses). Output: build/cross/*.vcvplugin
# macOS packages need Apple's SDK and are built by CI on macOS runners.
set -euo pipefail
cd "$(dirname "$0")/.."
image="${TOOLCHAIN_IMAGE:-ghcr.io/bloodbat/rack-plugin-toolchain-win-linux}"
platforms="${*:-win-x64 lin-x64}"
targets=$(for p in $platforms; do printf 'plugin-build-%s ' "$p"; done)
docker run --rm -v "$PWD":/plugin -e PLUGIN_DIR=/plugin --user root "$image" bash -c "
	set -e
	cd /home/build/rack-plugin-toolchain
	make $targets
	mkdir -p /plugin/build/cross
	cp plugin-build/*.vcvplugin /plugin/build/cross/
	# The toolchain runs as root; hand everything back to the caller.
	rm -rf /plugin/dep /plugin/dist
	chown -R $(id -u):$(id -g) /plugin/build
"
ls -la build/cross
