#pragma once
// Shoal: per-instance firmware state.
//
// Every mutable firmware global lives in one shoal::Context per shoal::Core
// (defined in FirmwareContext.inc). Firmware code reaches it through a
// thread-local "current context" pointer that a Scope sets for the duration
// of each Core call, so many cores can run in one process and on different
// threads.
//
// Convention for a former global `T name;` in namespace N: its definition
// (and any extern) becomes
//     SHOAL_STATE(T, name)
//     #define name name_ref()
// so every later use of `name` (also `N::name`) calls N::name_ref(), which
// FirmwareContext.inc defines as `return shoal::current()->name;`.

namespace shoal {

struct Context;  // defined in FirmwareContext.inc

// The context firmware code on this thread works on; null outside a Scope.
Context*& current();

// Makes c the current context until the scope ends; scopes nest.
struct Scope {
	explicit Scope(Context* c) : prev_(current()) { current() = c; }
	~Scope() { current() = prev_; }
	Scope(const Scope&) = delete;
	Scope& operator=(const Scope&) = delete;

private:
	Context* prev_;
};

// Reference to T; lets SHOAL_STATE name array types (Ref<int[4]> is int(&)[4]).
template <class T> using Ref = T&;

} // namespace shoal

#define SHOAL_STATE(TYPE, NAME) ::shoal::Ref<TYPE> NAME##_ref();
