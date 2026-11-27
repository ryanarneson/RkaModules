#!/bin/sh
# The plugin's vendored pffft must not meet libRack's: rka_prefix.h renames
# every symbol it exports to rka_pffft_*, and the plugin may neither export
# nor import a plain pffft_* symbol (Makefile, "The vendored pffft"). Lists
# the plugin binary's external symbols, defined and undefined, and fails on
# any named pffft_* (macOS's nm prefixes C symbols with an underscore; Linux
# does not). Windows plugins are cross-built: there is no plugin.dll to look
# at here, so the check is skipped.
#
# Usage: tools/check_pffft_symbols.sh [plugin binary]   (default: plugin.dylib or plugin.so)
set -e
lib=$1
if [ -z "$lib" ]; then
	case "$(uname -s)" in
	Darwin) lib=plugin.dylib ;;
	Linux) lib=plugin.so ;;
	*) echo "check_pffft_symbols: skipped on $(uname -s)"; exit 0 ;;
	esac
fi
if [ ! -f "$lib" ]; then
	echo "check_pffft_symbols: $lib not found (build the plugin first)" >&2
	exit 1
fi
case "$(uname -s)" in
Darwin) syms=$(nm -g "$lib") ;;
*) syms=$( { nm -g "$lib" 2>/dev/null; nm -D "$lib" 2>/dev/null; } ) ;;
esac
if [ -z "$syms" ]; then
	echo "check_pffft_symbols: nm found no symbols in $lib" >&2
	exit 1
fi
bad=$(printf '%s\n' "$syms" | awk '{ s = $NF; sub(/^_/, "", s); if (s ~ /^pffft_/) print }')
if [ -n "$bad" ]; then
	echo "check_pffft_symbols: $lib exports or imports plain pffft symbols (add them to src/dep/pffft/rka_prefix.h):" >&2
	printf '%s\n' "$bad" >&2
	exit 1
fi
renamed=$(printf '%s\n' "$syms" | awk '{ s = $NF; sub(/^_/, "", s); if (s ~ /^rka_pffft_/) n++ } END { print n + 0 }')
echo "check_pffft_symbols: no plain pffft_* symbol in $lib ($renamed renamed ones visible)"
