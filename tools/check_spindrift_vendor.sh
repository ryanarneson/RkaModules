#!/bin/sh
# Checks Spindrift's vendored Mutable Instruments Beads against upstream.
#
# - src/spindrift/testdata/ref/ (the reference the tests compare against)
#   must equal upstream byte for byte, file for file with the vendored copy.
# - Every difference between src/spindrift/{beads,stmlib}/ and upstream must
#   be on lines carrying `SPINDRIFT:` (a removed line leaves a marked line in
#   its place), unless the file is listed under "## Files rewritten" in
#   src/spindrift/CHANGES.md (as "- `beads/dsp/foo.h`"); a file upstream
#   does not have must be listed there.
#
# Upstream is pichenettes/eurorack at the commit below, with its stmlib
# submodule at the commit that pins; both are read from the checkout's git
# objects, so its working tree may be anywhere. Without the checkout (CI) the
# check says "skipped".
#
# Usage: tools/check_spindrift_vendor.sh
# SPINDRIFT_UPSTREAM overrides the checkout's path (default
# ~/ws/plugins/eurorack).
set -eu

EURORACK_COMMIT=f4cfa7697095a28d292bff758beb51deb49b5a86
STMLIB_COMMIT=e3bd7c9cc00e4364166f9905c0509b6ffd0535ec

cd "$(dirname "$0")/.."
UPSTREAM=${SPINDRIFT_UPSTREAM:-$HOME/ws/plugins/eurorack}
SRC=src/spindrift
REF=$SRC/testdata/ref

if [ ! -d "$UPSTREAM/.git" ] && [ ! -f "$UPSTREAM/.git" ]; then
	echo "check_spindrift_vendor: skipped (no upstream checkout at $UPSTREAM)"
	exit 0
fi
if ! git -C "$UPSTREAM" cat-file -e "$EURORACK_COMMIT^{commit}" 2>/dev/null ||
	! git -C "$UPSTREAM/stmlib" cat-file -e "$STMLIB_COMMIT^{commit}" 2>/dev/null; then
	echo "check_spindrift_vendor: $UPSTREAM lacks eurorack $EURORACK_COMMIT or stmlib $STMLIB_COMMIT" >&2
	exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/spindrift_vendor.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM

# upstream <path relative to the eurorack root> <out file>; fails if absent.
upstream() {
	case $1 in
	stmlib/*) git -C "$UPSTREAM/stmlib" show "$STMLIB_COMMIT:${1#stmlib/}" > "$2" 2>/dev/null ;;
	*) git -C "$UPSTREAM" show "$EURORACK_COMMIT:$1" > "$2" 2>/dev/null ;;
	esac
}

# Files exempt from line marks: "- `path`" lines under "## Files rewritten".
rewritten() {
	[ -f "$SRC/CHANGES.md" ] || return 1
	awk -v want="$1" '
		/^## / { in_section = ($0 == "## Files rewritten") ; next }
		in_section && index($0, "- `" want "`") == 1 { found = 1 }
		END { exit found ? 0 : 1 }' "$SRC/CHANGES.md"
}

ok=1
fail() {
	echo "check_spindrift_vendor: $*" >&2
	ok=0
}

(cd "$SRC" && find beads stmlib -type f | LC_ALL=C sort) > "$TMP/vendored"
(cd "$REF" && find beads stmlib -type f | LC_ALL=C sort) > "$TMP/ref"

# The reference: upstream exactly.
while IFS= read -r f; do
	if ! upstream "$f" "$TMP/up"; then
		fail "$REF/$f is not upstream"
	elif ! cmp -s "$TMP/up" "$REF/$f"; then
		fail "$REF/$f differs from upstream (the reference is never edited)"
	fi
done < "$TMP/ref"

# The vendored copy: upstream but for marked lines and rewritten files.
while IFS= read -r f; do
	if ! upstream "$f" "$TMP/up"; then
		rewritten "$f" || fail "$SRC/$f is not upstream and not under \"## Files rewritten\" in $SRC/CHANGES.md"
		continue
	fi
	if ! grep -qxF "$f" "$TMP/ref"; then
		fail "$SRC/$f has no reference copy in $REF/"
	fi
	if cmp -s "$TMP/up" "$SRC/$f" || rewritten "$f"; then
		continue
	fi
	# POSIX diff's default format: "12,13c12,14" hunk headers, "< " upstream
	# lines, "> " vendored lines. A pure deletion has no marked line.
	if ! diff "$TMP/up" "$SRC/$f" | awk -v file="$SRC/$f" '
		/^[0-9]+(,[0-9]+)?d[0-9]+$/ {
			printf "check_spindrift_vendor: %s: upstream lines %s removed without a SPINDRIFT: line in their place\n", file, $0 > "/dev/stderr"
			bad = 1
			next
		}
		/^[0-9]+(,[0-9]+)?[ac][0-9]+(,[0-9]+)?$/ {
			split($0, part, /[ac]/)
			line = part[2] + 0
			next
		}
		/^> / {
			if (index($0, "SPINDRIFT:") == 0) {
				printf "check_spindrift_vendor: %s:%d: changed line without SPINDRIFT: %s\n", file, line, substr($0, 3) > "/dev/stderr"
				bad = 1
			}
			line++
		}
		END { exit bad ? 1 : 0 }'; then
		ok=0
	fi
done < "$TMP/vendored"

while IFS= read -r f; do
	grep -qxF "$f" "$TMP/vendored" || fail "$REF/$f has no vendored copy in $SRC/"
done < "$TMP/ref"

if [ $ok = 1 ]; then
	echo "check_spindrift_vendor: $(wc -l < "$TMP/vendored" | tr -d ' ') vendored files match upstream but for marked changes; the reference is upstream's"
	exit 0
fi
exit 1
