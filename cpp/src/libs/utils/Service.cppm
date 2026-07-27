module;
// Light headers only, and the reason is a GCC 14 bug rather than taste — see the
// note on the store()/lookup()/drop() seam below. <unordered_map>, <typeindex> and
// <mutex> are in Service.cpp, where they reach no BMI.
//
// No <format> anywhere in this module either. Phase 2's rule allows one partition
// to carry it, but the only message here is a fixed prefix plus a type name, which
// concatenation builds for free.
#include <concepts>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
export module utils:service;

import :singleton;

// Re-exported: utils::error::Error is the E of get()'s result.
export import utils.error;

export namespace utils {

// ServiceLocator binds an interface to an implementation once, at startup, and
// hands the implementation back to anything that asks for the interface:
//
//     ServiceLocator::instance().register_service<Fetcher, CurlFetcher>();
//     ...
//     auto fetcher = ServiceLocator::instance().get<Fetcher>();
//
// The key is the *interface type itself* — typeid(Interface) — so there is no
// registry of string names to keep in sync, and asking for the wrong thing is a
// compile error rather than a lookup miss.
//
// What this replaces: cup.platform's `detail::current_http_get()`, a function
// static holding a function pointer, plus ScopedHttpGet to swap it. That pattern
// works and cup will keep it where it already is (Phase 3 is green and the seam is
// one function). But it does not scale — each new seam needs its own accessor, its
// own guard, and its own place to remember the default — and it forces the seam to
// be a *function*, which is why HttpGet is a bare function pointer and why the
// suites' stubs have to be captureless with their observations in file-scope
// variables. A locator keyed on an interface takes objects, so a stub can be a
// class with members, which is where Phase 4's command tests are heading.
//
// Deliberately *not* dependency injection. A locator is the pattern you reach for
// when the call sites are spread through a CLI's command handlers and threading a
// container through every one of them buys nothing; its known cost is that a
// type's dependencies no longer show up in its constructor signature. cup accepts
// that trade for platform seams — HTTP, the terminal, process spawning — and
// nowhere else. Construct ordinary collaborators ordinarily.
//
// It is a Singleton because a second locator would mean two answers to "what is
// bound to Fetcher", which is the one question it exists to answer. Tests reach
// for ScopedService below rather than for a second instance.
class ServiceLocator : public Singleton<ServiceLocator> {
public:
    // register_service constructs an Impl from args and binds it to Interface,
    // returning the constructed object so the caller can keep configuring it.
    //
    // This is the `register<Interface, Impl>` of the classic formulation, renamed
    // because `register` is a reserved keyword in C++ — removed as a storage-class
    // specifier in C++17, still unusable as an identifier.
    //
    // Construction is eager: the object is built here, not on first get(). Lazy
    // construction would mean storing a creator per binding and running it under
    // the lock on first use, and its usual justification — paying only for what a
    // run touches — does not apply to a CLI that binds three seams at startup and
    // exits in milliseconds. Eager also means a constructor that fails does so
    // during setup rather than deep inside a command.
    //
    // Rebinding an interface replaces the previous binding and drops this
    // locator's reference to it. Anything still holding the shared_ptr keeps a
    // valid object — that is what makes ScopedService's restore safe.
    template <typename Interface, std::derived_from<Interface> Impl, typename... Args>
        requires std::constructible_from<Impl, Args...>
    std::shared_ptr<Impl> register_service(Args&&... args) {
        auto service = std::make_shared<Impl>(std::forward<Args>(args)...);
        register_instance<Interface>(std::shared_ptr<Interface>(service));
        return service;
    }

    // register_instance binds an already-built object. register_service is the
    // usual door; this one is for a service that outlives the locator's knowledge
    // of how to build it — a ThreadPool sized from a command-line flag, say — and
    // it is what ScopedService restores through.
    template <typename Interface>
    void register_instance(std::shared_ptr<Interface> service) {
        // Erased *from* shared_ptr<Interface> rather than from shared_ptr<Impl>.
        // The distinction is load-bearing under multiple inheritance: the
        // conversion applies the base offset now, so the void* handed to store()
        // is already an Interface*, and the cast back in find() returns exactly
        // that pointer. Erasing the Impl pointer instead would hand out a
        // wrongly-offset address — not a crash, a silently corrupt object. The
        // control block is untouched by either conversion, so ~Impl still runs.
        store(typeid(Interface), std::static_pointer_cast<void>(std::move(service)));
    }

    // find returns the service bound to Interface, or nullptr if there is none.
    // The accessor for code that has something to do either way; get() is for code
    // that does not.
    template <typename Interface>
    [[nodiscard]] std::shared_ptr<Interface> find() const {
        return std::static_pointer_cast<Interface>(lookup(typeid(Interface)));
    }

    // get returns the service bound to Interface, or an Error naming the interface
    // that was never bound.
    //
    // The name in that message is typeid(Interface).name(), which is
    // implementation-mangled (`5Cache` on GCC's Itanium ABI, not `Cache`). Left
    // that way on purpose: cup does not link a demangler, and an unbound service is
    // a wiring mistake by whoever wrote main(), not a condition a user can act on.
    // Every message a *user* sees is built by cup.ui from a string cup wrote.
    template <typename Interface>
    [[nodiscard]] std::expected<std::shared_ptr<Interface>, error::Error> get() const {
        if (auto service = find<Interface>(); service != nullptr) {
            return service;
        }
        return std::unexpected(
            error::Error(std::string("no service registered for ") + typeid(Interface).name()));
    }

    // contains reports whether Interface is bound.
    template <typename Interface>
    [[nodiscard]] bool contains() const {
        return bound(typeid(Interface));
    }

    // unbind drops the binding for Interface and reports whether there was one.
    template <typename Interface>
    bool unbind() {
        return drop(typeid(Interface));
    }

    // clear drops every binding. Chiefly a test affordance; a command has no
    // reason to unwire the process it is running in.
    void clear();

    // size is the number of bound interfaces.
    [[nodiscard]] std::size_t size() const;

private:
    // Singleton::instance() constructs and destroys the one ServiceLocator, and
    // this is what lets it past the private special members below. Without the
    // pair, deriving from Singleton would remove copying and guarantee nothing
    // else — see the note there.
    friend class Singleton<ServiceLocator>;
    ServiceLocator();
    ~ServiceLocator();

    // The four non-template operations on the map, declared here and defined in
    // Service.cpp. Everything public above is a template that erases to one of
    // them and does no container work itself.
    //
    // That split is not a preference, it is the workaround for a GCC 14 module
    // bug — the sixth this port has found, and the first that is not about where a
    // header sits. Instantiating std::unordered_map from a template that an
    // interface unit exports fails in the *consumer*, because the standard
    // library's own helpers come back module-attached and their partial
    // specialisations are then not found:
    //
    //     error: invalid use of incomplete type
    //            'struct std::__detail::_Select1st@utils::__1st_type<...>'
    //
    // pointed at <bits/hashtable_policy.h>, with the whole instantiation stack
    // suffixed `@utils`. The map itself is fine; instantiating it *across the
    // module boundary* is not. Keeping every container operation in a non-template
    // defined in an implementation unit means the instantiation happens once,
    // there, in an ordinary translation unit — and it costs nothing, because the
    // erased key and the erased pointer are all these functions ever needed. See
    // docs/migration-cpp23.md, rule 6.
    //
    // The interface is std::type_info rather than std::type_index for the same
    // reason it is worth having: typeid() yields one directly, so <typeindex> stays
    // out of this file too.
    void store(const std::type_info& interface, std::shared_ptr<void> service);
    [[nodiscard]] std::shared_ptr<void> lookup(const std::type_info& interface) const;
    [[nodiscard]] bool bound(const std::type_info& interface) const;
    bool drop(const std::type_info& interface);

    // State holds the map and the mutex guarding it. Defined in Service.cpp;
    // incomplete here, which is why the constructor and destructor are declared
    // and not defaulted (unique_ptr's deleter needs the complete type).
    struct State;
    std::unique_ptr<State> state_;
};

// ScopedService binds a service for the lifetime of the guard and restores
// whatever was bound before — including nothing — on the way out.
//
//     ScopedService<Fetcher> guard(std::make_shared<FakeFetcher>());
//
// This is the same shape as cup.platform's ScopedHttpGet and cup.ui's ScopedInput,
// and it exists for the same reason: a process-wide binding that a test needs to
// change must be put back, or the next test in the binary inherits it. Catch2 runs
// every case in one process, so "the next test" is not hypothetical.
//
// Not thread-safe against concurrent rebinding of the same interface, and cannot
// be: two guards for one interface overlapping in time have no correct restore
// order. Construct them where the test does, not inside a task.
template <typename Interface>
class ScopedService {
public:
    explicit ScopedService(std::shared_ptr<Interface> service) {
        ServiceLocator::instance().register_instance<Interface>(std::move(service));
    }

    ScopedService(const ScopedService&) = delete;
    ScopedService& operator=(const ScopedService&) = delete;
    ScopedService(ScopedService&&) = delete;
    ScopedService& operator=(ScopedService&&) = delete;

    ~ScopedService() {
        if (previous_ != nullptr) {
            ServiceLocator::instance().register_instance<Interface>(std::move(previous_));
        } else {
            ServiceLocator::instance().unbind<Interface>();
        }
    }

private:
    // Captured by the default member initializer, which runs before the
    // constructor body — so previous_ holds the binding installed on the way in,
    // not the one the body installs. (Same trick as ScopedHttpGet and ScopedInput,
    // and the reason the constructor body cannot be moved into the initializer
    // list.)
    std::shared_ptr<Interface> previous_ = ServiceLocator::instance().find<Interface>();
};

}  // namespace utils
