#!/bin/sh
# Checks tools/check_shoal_globals.sh on small ELF probe objects, so its ELF
# branch can be tested on a Mac: cross-compiles each probe with clang for
# x86_64 Linux under ASan/UBSan (as the Shoal tests build) and runs the
# check with SHOAL_OBJ_FORMAT=elf. Skips if clang cannot target ELF.
# Not part of `make test`.
#
# Usage: tools/test_check_shoal_globals.sh
set -u

here=$(cd "$(dirname "$0")" && pwd)
check="$here/check_shoal_globals.sh"
CLANG=${CLANG:-clang++}

tmp=$(mktemp -d 2>/dev/null || mktemp -d -t shoalglobals)
trap 'rm -rf "$tmp"' EXIT INT TERM

compile() { # name -> $tmp/name.o from $tmp/name.cpp
	"$CLANG" -target x86_64-unknown-linux-gnu -std=c++17 -O1 \
		-fsanitize=address,undefined -c "$tmp/$1.cpp" -o "$tmp/$1.o" 2>"$tmp/$1.err"
}

# Can clang target ELF at all?
printf 'int shoal_probe;\n' >"$tmp/probe.cpp"
if ! command -v "$CLANG" >/dev/null 2>&1 || ! compile probe; then
	echo "SKIP: $CLANG cannot compile for x86_64-unknown-linux-gnu"
	exit 0
fi

# Probes. No #include, so no Linux sysroot is needed.
cat >"$tmp/const_ok.cpp" <<'EOF'
// Read-only tables and a dynamically initialised static const member named
// like the allow-listed settings::SettingsBase<...>::storage_size_ (lands in
// .bss). ASan adds a writable __odr_asan_gen_* byte for each of them.
extern const char* const string_table[];
const char* const string_table[] = { "a", "b" };
extern const unsigned short lut[];
const unsigned short lut[] = { 1, 2, 3 };
unsigned long calc(unsigned long n); // defined elsewhere: forces dynamic init
template <class T> struct SettingsBase { static const unsigned long storage_size_; };
template <> const unsigned long SettingsBase<int>::storage_size_ = calc(3);
unsigned long use() { return SettingsBase<int>::storage_size_ + lut[1]; }
EOF
cat >"$tmp/writable_global.cpp" <<'EOF'
int probe_writable_global = 42;
int probe_use() { return probe_writable_global++; }
EOF
cat >"$tmp/writable_static_member.cpp" <<'EOF'
struct ProbeClass { static int probe_static_member; };
int ProbeClass::probe_static_member = 7;
int probe_use() { return ProbeClass::probe_static_member++; }
EOF
cat >"$tmp/zero_global.cpp" <<'EOF'
int probe_zero_global;
int probe_use() { return probe_zero_global++; }
EOF
cat >"$tmp/eh_personality_ok.cpp" <<'EOF'
// A destructor on an unwinding path: the compiler emits
// DW.ref.__gxx_personality_v0 in a writable section (filled in at load time).
struct ProbeGuard { ~ProbeGuard(); };
void probe_may_throw();
void probe_use() { ProbeGuard g; probe_may_throw(); }
EOF
cat >"$tmp/current_context_ok.cpp" <<'EOF'
// The one allowed thread-local: shoal::current()'s context pointer, as in
// FirmwareContext.inc.
namespace shoal {
struct Context;
Context*& current() {
	static thread_local Context* context = nullptr;
	return context;
}
}
EOF
cat >"$tmp/second_thread_local.cpp" <<'EOF'
// The allowed thread-local plus one more: the second must be rejected.
namespace shoal {
struct Context;
Context*& current() {
	static thread_local Context* context = nullptr;
	return context;
}
}
thread_local int probe_thread_local;
int probe_use() { return probe_thread_local++; }
EOF

failures=0

# expect NAME pass|fail [SYMBOL]
expect() {
	if ! compile "$1"; then
		echo "FAIL $1: does not compile"; cat "$tmp/$1.err"
		failures=$((failures + 1)); return
	fi
	SHOAL_OBJ_FORMAT=elf "$check" "$tmp/$1.o" >"$tmp/$1.out" 2>&1
	status=$?
	if [ "$2" = pass ]; then
		if [ $status -eq 0 ]; then
			echo "ok   $1: passes"
		else
			echo "FAIL $1: expected to pass, exit $status:"; sed 's/^/     /' "$tmp/$1.out"
			failures=$((failures + 1))
		fi
	else
		if [ $status -eq 1 ] && grep -q "$3" "$tmp/$1.out"; then
			echo "ok   $1: fails and names $3"
		else
			echo "FAIL $1: expected exit 1 naming $3, got exit $status:"; sed 's/^/     /' "$tmp/$1.out"
			failures=$((failures + 1))
		fi
	fi
}

expect const_ok pass
expect writable_global fail probe_writable_global
expect writable_static_member fail 'ProbeClass::probe_static_member'
expect zero_global fail probe_zero_global
expect current_context_ok pass
expect second_thread_local fail probe_thread_local
expect eh_personality_ok pass

if [ $failures -ne 0 ]; then
	echo "test_check_shoal_globals: $failures probe(s) failed"
	exit 1
fi
echo "test_check_shoal_globals: all probes behaved as expected"
