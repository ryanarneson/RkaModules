#!/bin/sh
# A package holds the plugin's files and nothing else. macOS's tar writes an
# AppleDouble entry ("._name") beside every file that carries Mac metadata,
# even with --no-xattrs, unless COPYFILE_DISABLE is set (the Makefile sets
# it). A Mac unpacks those back into metadata and never shows them, and its
# tar does not list them either; anything else unpacks them as files. A Mac
# package unpacked in a Windows Rack's plugins folder left "._1_Becalmed" and
# its like in Fog's Preset menu, each a JSON error (issue #123). Lists the
# archive with Python's tar reader, which hides nothing, and fails on any
# "._" entry or .DS_Store.
#
# Usage: tools/check_package_files.sh [package ...]   (default: dist/*.vcvplugin)
set -e
[ $# -gt 0 ] || set -- dist/*.vcvplugin
status=0
for pkg in "$@"; do
	[ -f "$pkg" ] || { echo "check_package_files: no package $pkg" >&2; exit 1; }
	zstd -dc "$pkg" | python3 -c '
import os, sys, tarfile
names = tarfile.open(fileobj=sys.stdin.buffer, mode="r|").getnames()
bad = [n for n in names if os.path.basename(n).startswith("._") or os.path.basename(n) == ".DS_Store"]
for n in bad[:20]:
    print("  " + n)
print("%s: %d entries, %d that are not the plugin%s" % (sys.argv[1], len(names), len(bad), chr(39) + "s"))
sys.exit(1 if bad or not names else 0)
' "$pkg" || status=1
done
exit $status
