// utils is the small set of patterns the rest of cup builds on: a singleton, a
// service locator, an abstract factory, a thread pool — the four object-lifetime
// ones — and a strong typedef, which is about signatures rather than lifetimes.
//
// It exists because the port arrived from Go, and Go's answer to every one of
// these is a package-level variable plus an `init()`. That shape survived the
// mechanical translation — cup.platform keeps its installed fetcher in a function
// static, cup.ui keeps the colour flag in another, and cup.scaffold reaches for a
// bare `std::async` per concurrent fetch. Each is fine on its own and none of them
// composes: there is no way to ask what is installed, no way to bind a second seam
// without writing a third accessor, and no way to bound concurrency across two
// call sites that do not know about each other. The patterns here are the C++
// answers to those questions, and they are deliberately the *classic* ones, so a
// C++ contributor recognises the shape before reading the comment.
//
// Five partitions, one idea each:
//
//   :singleton    Singleton<Derived>     — CRTP, one instance, no second one possible
//   :service      ServiceLocator         — register_service<Interface, Impl>() / get<Interface>()
//   :factory      Factory<Product, …>    — key -> creator, resolved at run time
//   :pool         ThreadPool             — bounded workers, std::future results
//   :strong_type  StrongType<Value, Tag> — a value with its own type, opt-in operations
//
// The first four compose rather than stack: ServiceLocator *is* a Singleton (there
// is one per process, which is the whole point of a locator), a Factory is a value
// a caller owns, and a ThreadPool is typically a service. The fifth is orthogonal
// to all of them — it is about what a *signature* can express, not about who owns
// what — and it is the one partition here with no lifetime opinion at all.
//
// The only thing this module imports is utils.error, its sibling under
// src/libs/utils, so the pair sits at the bottom of the graph and imports nothing
// from cup at all.
//
// Which is why it is `utils` and not `cup.utils`. Module names come from the path
// under src/libs, so the directory is the naming decision, and this library lives
// at src/libs/utils rather than under src/libs/cup with the rest: nothing in it
// names a cup concept — no project, no template, no terminal — and a locator keyed
// on typeid is the same locator in any program. utils.error is here for the same
// reason, one level down: an E carrying a message is not a cup idea either, and it
// is what made this module's last tie to cup/ go away.
//
// ---
//
// The global module fragment below is required even though this file declares
// nothing: GCC 14 cannot produce a BMI its consumers can read for a module whose
// partitions carry a fragment unless the primary carries one too. `cup add lib`
// now scaffolds this preamble automatically (see cmd.primaryPreamble).
//
// On the Phase 2 rule that at most one partition may reach the heavy standard
// library: this module tests its limit rather than respecting it, because there is
// no arrangement of the four lifetime patterns that avoids <memory> in three of
// them. It merges on GCC 14.2 — see the note in Pool.cppm for what is kept out of a
// BMI to make that true, and docs/migration-cpp23.md for the restated rule.
// :strong_type stays on the light headers and adds nothing to that budget, which is
// also why it has no Printable skill.
module;
#include <string>
export module utils;

// Re-exported because utils::error::Error is the E of every result in :service and
// :factory, and a module re-exports nothing it merely imports.
//
// The partitions write it as plain `error::Error`, and what makes that resolve is
// enclosing-namespace lookup rather than a using-directive: they are in namespace
// utils, and utils::error is nested in it. Worth knowing, because it is exactly
// what the cup modules lost when the error type moved here — every one of them now
// spells it `utils::error::Error` in full.
export import utils.error;

export import :singleton;
export import :service;
export import :factory;
export import :pool;
export import :strong_type;
