#!/bin/sh
# Fails if an object file defines a symbol in a writable data section.
#
# Used on the Shoal firmware object (build/test/shoal/Firmware.o): every
# mutable firmware global lives in a per-instance shoal::Context, so the
# object must not hold any writable global state. Allowed: read-only data
# (including relocated read-only data), thread-local data, sanitizer
# bookkeeping, and assembler-local labels (section starts and the
# sanitizers' unnamed descriptors, which no source object can produce).
#
# Usage: tools/check_shoal_globals.sh path/to/object.o
# The object format follows `uname -s` (Darwin: Mach-O, else ELF); set
# SHOAL_OBJ_FORMAT=macho or elf to override (tools/test_check_shoal_globals.sh
# uses it to check ELF objects on a Mac).
#
# Regexes are passed to awk with -v, which processes backslash escapes, so
# they use bracket expressions ([.]) instead of backslashes.
set -eu

if [ $# -ne 1 ] || [ ! -f "$1" ]; then
	echo "usage: $0 object-file" >&2
	exit 2
fi

if command -v llvm-objdump >/dev/null 2>&1; then
	OBJDUMP=llvm-objdump
elif command -v objdump >/dev/null 2>&1; then
	OBJDUMP=objdump
else
	echo "$0: needs llvm-objdump or objdump" >&2
	exit 2
fi

if command -v c++filt >/dev/null 2>&1; then
	DEMANGLE=c++filt
else
	DEMANGLE=cat
fi

# Symbols that sit in a writable section but are read-only after load.
# One extended regex (alternatives joined with |), matched against the
# raw (mangled) name.
#   storage_size_: settings::SettingsBase<...>::storage_size_ is declared
#   `static const size_t` and computed once during static initialisation
#   (before main, single-threaded); nothing writes it afterwards.
#   DW.ref.*: GCC's pointer to the C++ exception personality routine
#   (DW.ref.__gxx_personality_v0, in .data.rel.local with -fPIC), filled in
#   by relocation at load time and only read by the unwinder.
ALLOWED='13storage_size_E$|^DW[.]ref[.]'

FORMAT=${SHOAL_OBJ_FORMAT:-}
if [ -z "$FORMAT" ]; then
	case "$(uname -s)" in
	Darwin) FORMAT=macho ;;
	*) FORMAT=elf ;;
	esac
fi

case "$FORMAT" in
macho)
	# Mach-O: "addr flags... SEGMENT,section name". Assembler-local
	# labels start with "l" or "L" (C and C++ names start with "_").
	WRITABLE='^__DATA,__(data|bss|common)$'
	LOCAL='^(ltmp[0-9]+|l_.*|L.*)$'
	;;
elf)
	# ELF: "addr flags... section size name"; COMMON shows as *COM*.
	# -fdata-sections gives .data.<name> / .bss.<name>; .data.rel.ro is
	# read-only after relocation (skipped below). Assembler-local labels
	# start with ".L".
	WRITABLE='^([.]data([.].*)?|[.]bss([.].*)?|[*]COM[*]|COMMON)$'
	LOCAL='^[.]L'
	;;
*)
	echo "$0: SHOAL_OBJ_FORMAT must be macho or elf, not '$FORMAT'" >&2
	exit 2
	;;
esac

found=$("$OBJDUMP" -t "$1" | awk -v writable="$WRITABLE" -v loc="$LOCAL" -v allowed="$ALLOWED" '
	{
		sect = ""
		for (i = 2; i < NF; i++)
			if ($i ~ writable) { sect = $i; break }
		if (sect == "" || sect ~ /^\.data\.rel\.ro/)
			next
		name = $NF
		if (name == sect)                              next # ELF section symbol
		if (name ~ /^_*(odr_asan|asan|ubsan)/)         next # sanitizer bookkeeping
		if (name ~ loc)                                next # assembler-local labels
		if (name ~ allowed)                            next
		print sect "\t" name
	}')

if [ -n "$found" ]; then
	n=$(printf '%s\n' "$found" | wc -l | tr -d ' ')
	echo "$1: $n symbol(s) in writable data sections:" >&2
	printf '%s\n' "$found" | $DEMANGLE >&2
	exit 1
fi
