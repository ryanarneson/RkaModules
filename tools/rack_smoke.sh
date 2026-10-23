#!/usr/bin/env bash
# Load the plugin in a real Rack, with a throwaway user folder, and render a
# screenshot of every module. Catches what unit tests can't: the plugin
# failing to load, a widget crashing on construction, a broken panel SVG.
#
# Needs a Rack install (RACK_APP, default ~/ws/sdk/rack-app/Rack2Free) and,
# without a display, xvfb-run. Usage: tools/rack_smoke.sh [dist/*.vcvplugin]
set -euo pipefail

RACK_APP="${RACK_APP:-$HOME/ws/sdk/rack-app/Rack2Free}"
cd "$(dirname "$0")/.."
slug=$(jq -r .slug plugin.json)
pkg="${1:-$(ls -t dist/*.vcvplugin | head -1)}"

user=$(mktemp -d)
trap 'rm -rf "$user"' EXIT
# Rack wants one plugins dir per arch; take it from the package name.
arch=$(basename "$pkg" .vcvplugin | sed -E 's/.*-([a-z]+-[a-z0-9]+)$/\1/')
mkdir -p "$user/plugins-$arch"
cp "$pkg" "$user/plugins-$arch/"

run=(./Rack -u "$user" -t 2)
[ -z "${DISPLAY:-}" ] && run=(xvfb-run -a -s "-screen 0 1280x720x24" "${run[@]}")
# Rack's RPATH is ".", so it must be started from its own folder.
(cd "$RACK_APP" && timeout 120 "${run[@]}" >/dev/null 2>&1) || true

log="$user/log.txt"
if ! grep -q "Loaded plugin $slug " "$log"; then
	echo "FAIL: Rack did not load $slug"; grep -iE "warn|fatal|$slug" "$log" | tail -20; exit 1
fi
if grep -iE "\[(warn|fatal)" "$log" | grep -i "$slug"; then
	echo "FAIL: warnings mentioning $slug"; exit 1
fi

mkdir -p build/screenshots
missing=0
for m in $(jq -r '.modules[].slug' plugin.json); do
	png="$user/screenshots/$slug/$m.png"
	if [ -s "$png" ]; then cp "$png" build/screenshots/; echo "ok: $m -> build/screenshots/$m.png"
	else echo "FAIL: no screenshot for $m"; missing=1; fi
done
exit $missing
