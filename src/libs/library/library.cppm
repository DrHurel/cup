module;
#include <cstddef>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <vector>
export module library;

// Every library in src/libs exports a `Library` class that declares, at
// compile time, the other libraries it depends on, plus the static
// initialize() and finalize() hooks that set it up and tear it down:
//
//     export struct Library {
//         using dependencies = std::tuple<error::Library, core::Library>;
//         static void initialize();
//         static void finalize();
//     };
//
// dependencies names exactly the libraries its target links in CMake; a
// library whose module does not import another cannot name it, and module
// imports cannot form a cycle, so neither can this graph.
//
// initialize<L>() brings up L's whole dependency graph, depth-first and
// dependencies before dependents, each library once however many paths lead
// to it; finalize() tears down in exactly the reverse order. Scope<L> pairs
// the two for a block (cmd::run_main holds one for the whole command).
//
// The bookkeeping is a plain process-wide stack: cup initializes from one
// thread, before it starts any other.

namespace library {

template <class T>
struct is_tuple : std::false_type {};
template <class... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

}  // namespace library

export namespace library {

// Declaration is what a Library class must provide.
template <class L>
concept Declaration = requires {
    typename L::dependencies;
    requires is_tuple<typename L::dependencies>::value;
    { L::initialize() } -> std::same_as<void>;
    { L::finalize() } -> std::same_as<void>;
};

// depends_on<L, D> is true when D is among L's dependencies, directly or
// through one of them.
template <Declaration L, class D>
struct depends_on;

}  // namespace library

namespace library {

template <class D, class Tuple>
struct any_depends_on;
template <class D, class... Ls>
struct any_depends_on<D, std::tuple<Ls...>>
    : std::bool_constant<((std::is_same_v<Ls, D> || depends_on<Ls, D>::value) || ...)> {};

// One initialized library: the flag initialize<L>() checks, and how to
// finalize it.
struct Entry {
    bool* initialized;
    void (*finalize)();
};

// is_initialized<L>() is true from L's initialize() until its finalize().
template <class L>
bool& is_initialized() {
    static bool initialized = false;
    return initialized;
}

// The libraries initialized so far, in initialization order.
std::vector<Entry>& initialized_stack() {
    static std::vector<Entry> stack;
    return stack;
}

}  // namespace library

export namespace library {

template <Declaration L, class D>
struct depends_on : any_depends_on<D, typename L::dependencies> {};

template <Declaration L, class D>
inline constexpr bool depends_on_v = depends_on<L, D>::value;

// The number of libraries currently initialized.
[[nodiscard]] std::size_t initialized_count() { return initialized_stack().size(); }

// initialize brings up L after everything it depends on, skipping any
// library already initialized. If one's initialize() throws, the libraries
// initialized before it stay up (see Scope, which finalizes them).
template <Declaration L>
void initialize() {
    if (is_initialized<L>()) {
        return;
    }
    []<class... Ds>(std::type_identity<std::tuple<Ds...>>) {
        (initialize<Ds>(), ...);
    }(std::type_identity<typename L::dependencies>{});
    L::initialize();
    is_initialized<L>() = true;
    initialized_stack().push_back(Entry{&is_initialized<L>(), &L::finalize});
}

// finalize tears down, most recently initialized first, every library
// initialized after the first `keep` — all of them by default.
void finalize(std::size_t keep = 0) {
    auto& stack = initialized_stack();
    while (stack.size() > keep) {
        const Entry entry = stack.back();
        stack.pop_back();
        *entry.initialized = false;
        entry.finalize();
    }
}

// Scope initializes L's graph for its lifetime and, on the way out, finalizes
// what it initialized itself: libraries an enclosing Scope already brought up
// stay up until that one ends.
template <Declaration L>
class Scope {
public:
    Scope() : mark_(initialized_count()) {
        try {
            initialize<L>();
        } catch (...) {
            finalize(mark_);
            throw;
        }
    }
    ~Scope() { finalize(mark_); }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

private:
    std::size_t mark_;
};

// library itself depends on nothing and holds no state to set up.
struct Library {
    using dependencies = std::tuple<>;
    static void initialize() {
        // Nothing to set up.
    }
    static void finalize() {
        // Nothing to tear down.
    }
};

}  // namespace library
