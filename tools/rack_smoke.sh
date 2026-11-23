#!/usr/bin/env bash
# Load the plugin in a real Rack, with a throwaway user folder, and render a
# screenshot of every module. Catches what unit tests can't: the plugin
# failing to load, a widget crashing on construction, a broken panel SVG.
#
# Finds Rack on Linux (~/ws/sdk/rack-app/Rack2Free) and macOS
# (/Applications/VCV Rack 2*.app); set RACK_BIN to the Rack executable to
# override. Linux without a display needs xvfb-run.
# Usage: tools/rack_smoke.sh [dist/*.vcvplugin]
set -euo pipefail

cd "$(dirname "$0")/.."
slug=$(jq -r .slug plugin.json)
pkg="${1:-$(ls -t dist/*.vcvplugin | head -1)}"

if [ -z "${RACK_BIN:-}" ]; then
	case "$(uname -s)" in
	Darwin)
		for app in "/Applications/VCV Rack 2 Free.app" "/Applications/VCV Rack 2 Pro.app"; do
			[ -x "$app/Contents/MacOS/Rack" ] && RACK_BIN="$app/Contents/MacOS/Rack" && break
		done ;;
	*)
		RACK_BIN="$HOME/ws/sdk/rack-app/Rack2Free/Rack" ;;
	esac
fi
[ -x "${RACK_BIN:-}" ] || { echo "Rack not found; set RACK_BIN"; exit 1; }

user=$(mktemp -d)
trap 'rm -rf "$user"' EXIT
# Rack wants one plugins dir per arch; take it from the package name.
arch=$(basename "$pkg" .vcvplugin | sed -E 's/.*-([a-z]+-[a-z0-9]+)$/\1/')
mkdir -p "$user/plugins-$arch"
cp "$pkg" "$user/plugins-$arch/"

# A WAV for Eddy (never committed: build/ is wiped by make clean and audio is
# git-ignored): 20 s of a swelling chord at 48 kHz, so loading it into the
# headless engine (44.1 kHz) also converts its rate. The screenshot's Eddy loads it (RKA_EDDY_PREVIEW_WAV);
# the headless runs below load it into a running module.
mkdir -p build/smoke
wav="$(pwd)/build/smoke/eddy.wav"
python3 - "$wav" <<'EOF'
import array, math, sys, wave
fs, n = 48000, 20 * 48000
a = array.array("h")
for i in range(n):
    t = i / fs
    env = 0.25 + 0.55 * math.sin(math.pi * t / 20.0) ** 2 * (0.75 + 0.25 * math.cos(2 * math.pi * t / 7.0))
    x = env * (0.5 * math.sin(2 * math.pi * 110 * t) + 0.3 * math.sin(2 * math.pi * 165 * t) + 0.2 * math.sin(2 * math.pi * 220.5 * t))
    a.append(int(32767 * x))
    a.append(int(32767 * x * 0.9))
if sys.byteorder == "big":
    a.byteswap()
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(fs)
    w.writeframes(a.tobytes())
EOF

# Rack, under a time limit; macOS has no `timeout` unless coreutils is
# installed (as gtimeout).
limit=()
for t in timeout gtimeout; do
	command -v "$t" >/dev/null && limit=("$t" 120) && break
done
run=(${limit[@]+"${limit[@]}"} "$RACK_BIN" -u "$user" -t 2)
if [ "$(uname -s)" = Linux ] && [ -z "${DISPLAY:-}" ]; then
	run=(xvfb-run -a -s "-screen 0 1280x720x24" "${run[@]}")
fi
# The Linux build's RPATH is ".", so Rack must start from its own folder.
(cd "$(dirname "$RACK_BIN")" && RKA_EDDY_PREVIEW_WAV="$wav" "${run[@]}" >/dev/null 2>&1) || true

log="$user/log.txt"
cp "$log" build/smoke/log-screenshots.txt
if ! grep -q "Loaded plugin $slug " "$log"; then
	echo "FAIL: Rack did not load $slug"; grep -iE "warn|fatal|$slug" "$log" | tail -20; exit 1
fi
if grep -iE "\[(warn|fatal)" "$log" | grep -i "$slug"; then
	echo "FAIL: warnings mentioning $slug"; exit 1
fi

# Eddy in a running (headless) Rack, a patch whose Eddy loads the WAV
# (Review Focus 4): once quit at once, so the module is deleted while its
# worker loads; once quit after a few seconds, with the tape playing. Rack
# must not crash, and log no warning from Eddy. Logs: build/smoke/.
ver=$(jq -r .version plugin.json)
patch="$user/eddy-smoke.vcv"
jq -n --arg wav "$wav" --arg ver "$ver" --arg slug "$slug" '{version: "2.6.6", modules: [{id: 1, plugin: $slug,
	model: "Eddy", version: $ver, params: [{id: 10, value: 1}, {id: 11, value: 1}], data: {file: $wav, keepTape: false},
	pos: [0, 0]}], cables: []}' > "$patch"
for wait in 0 4; do
	status=0
	(cd "$(dirname "$RACK_BIN")" && (sleep "$wait"; echo) | ${limit[@]+"${limit[@]}"} "$RACK_BIN" -u "$user" -h "$patch" >/dev/null 2>&1) || status=$?
	cp "$log" "build/smoke/log-headless-$wait.txt"
	if [ "$status" != 0 ] || grep -iqE "fatal|crash" "$log"; then
		echo "FAIL: headless Rack with Eddy (quit after ${wait} s) exited $status"; grep -iE "fatal|crash|eddy" "$log" | tail -20; exit 1
	fi
	if grep -iE "\[[0-9.]+ (warn|fatal)" "$log" | grep -i "eddy"; then
		echo "FAIL: Eddy warned (quit after ${wait} s)"; exit 1
	fi
	grep -q "Loaded plugin $slug " "$log" || { echo "FAIL: headless Rack did not load $slug"; exit 1; }
	if [ "$wait" != 0 ] && ! grep -q "Eddy: loaded $wav" "$log"; then
		echo "FAIL: Eddy did not load the WAV in ${wait} s"; exit 1
	fi
	echo "ok: Eddy headless, quit after ${wait} s"
done

# Eddy's stored tape, from the patch's storage (Review Focus 5): an autosave
# folder whose Eddy keeps its tape, the three stored files made here at
# 48 kHz (the tape and the original different), so the 44.1 kHz headless
# engine loads them converted: loaded, and not taken as what is stored.
auto="$user/autosave"
rm -rf "$auto"
mkdir -p "$auto/modules/1"
jq -n --arg ver "$ver" --arg slug "$slug" '{version: "2.6.6", modules: [{id: 1, plugin: $slug, model: "Eddy",
	version: $ver, params: [{id: 10, value: 1}, {id: 11, value: 1}], data: {file: "", keepTape: true}, pos: [0, 0]}],
	cables: []}' > "$auto/patch.json"
python3 - "$auto/modules/1" <<'EOF2'
import math, struct, sys
def wav(path, frames):
    data = b"".join(struct.pack("<ff", l, r) for l, r in frames)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 2, 48000, 48000 * 8, 8, 32)
    open(path, "wb").write(hdr + b"data" + struct.pack("<I", len(data)) + data)
n = 48000
wav(sys.argv[1] + "/tape.wav", [(3 * math.sin(i * 0.01), 3 * math.sin(i * 0.011)) for i in range(n + 480)])
wav(sys.argv[1] + "/original.wav", [(2 * math.sin(i * 0.02), 2 * math.sin(i * 0.021)) for i in range(n + 480)])
wav(sys.argv[1] + "/tails.wav", [(480.0, 480.0)])
EOF2
status=0
(cd "$(dirname "$RACK_BIN")" && (sleep 2; echo) | ${limit[@]+"${limit[@]}"} "$RACK_BIN" -u "$user" -h >/dev/null 2>&1) || status=$?
cp "$log" build/smoke/log-headless-stored.txt
if [ "$status" != 0 ] || grep -iqE "fatal|crash" "$log" || grep -iE "\[[0-9.]+ (warn|fatal)" "$log" | grep -iq eddy; then
	echo "FAIL: headless Rack with Eddy's stored tape (exit $status)"; grep -iE "fatal|crash|eddy" "$log" | tail -20; exit 1
fi
if ! grep -q "Eddy: loaded the stored tape (1.00 s); not as stored" "$log"; then
	echo "FAIL: Eddy did not load its stored tape converted"; grep -i eddy "$log" | tail; exit 1
fi
echo "ok: Eddy headless, the stored tape loaded at another rate"

# Spindrift's frozen buffer, from the patch's storage: an autosave whose
# Spindrift was saved frozen, with buffer.bin the golden version 1 file
# (src/spindrift/testdata/GoldenBufferV1.hpp: its header's bytes and its
# payload's formula), so the patch's data reaches the module before onAdd
# and the file is there when onAdd reads it: restored. Then the same file
# cut short: not loaded, a warning, no crash.
for case in whole truncated; do
	rm -rf "$auto"
	mkdir -p "$auto/modules/1"
	jq -n --arg ver "$ver" --arg slug "$slug" '{version: "2.6.6", modules: [{id: 1, plugin: $slug, model: "Spindrift",
		version: $ver, params: [], data: {version: 1, quality: 2, seedLatched: true, frozen: true, synth: 0, gain: 0,
		manualGain: 1.0, gateOutput: false, keepFrozen: true}, pos: [0, 0]}], cables: []}' > "$auto/patch.json"
	python3 - "$auto/modules/1/buffer.bin" "$case" <<'EOF3'
import sys
head = bytes([0x53, 0x50, 0x44, 0x52, 1, 0, 0, 0, 2, 1, 0, 0, 0x41, 0xE2, 0x01, 0x00, 0x10, 0xB8, 0x0B, 0x00])
n = 768016 if sys.argv[2] == "whole" else 700000
payload = bytes((((i * 2654435761) & 0xFFFFFFFF) >> 24) ^ (i & 0xFF) for i in range(n))
open(sys.argv[1], "wb").write(head + payload)
EOF3
	status=0
	(cd "$(dirname "$RACK_BIN")" && (sleep 2; echo) | ${limit[@]+"${limit[@]}"} "$RACK_BIN" -u "$user" -h >/dev/null 2>&1) || status=$?
	cp "$log" "build/smoke/log-headless-spindrift-$case.txt"
	if [ "$status" != 0 ] || grep -iqE "fatal|crash" "$log"; then
		echo "FAIL: headless Rack with Spindrift's stored buffer ($case, exit $status)"; grep -iE "fatal|crash|spindrift" "$log" | tail; exit 1
	fi
	if [ "$case" = whole ] && ! grep -q "Spindrift: restored the frozen buffer (VINTAGE DIGITAL, mono)" "$log"; then
		echo "FAIL: Spindrift did not restore its stored buffer"; grep -i spindrift "$log" | tail; exit 1
	fi
	if [ "$case" = truncated ] && ! grep -qE "warn.*Spindrift: the frozen buffer .* was not loaded" "$log"; then
		echo "FAIL: Spindrift did not refuse a truncated buffer with a warning"; grep -i spindrift "$log" | tail; exit 1
	fi
	echo "ok: Spindrift headless, a stored buffer ($case)"
done

mkdir -p build/screenshots
missing=0
for m in $(jq -r '.modules[].slug' plugin.json); do
	png="$user/screenshots/$slug/$m.png"
	if [ -s "$png" ]; then cp "$png" build/screenshots/; echo "ok: $m -> build/screenshots/$m.png"
	else echo "FAIL: no screenshot for $m"; missing=1; fi
done
exit $missing
