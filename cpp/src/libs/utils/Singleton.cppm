// No global module fragment: this partition needs no standard headers at all, and
// a fragment it does not use is one more thing for GCC 14 to merge. The primary
// carries one for both of us (see utils.cppm).
export module utils:singleton;

export namespace utils {

// Singleton gives a type exactly one instance and takes away every way of making
// a second. It is the CRTP form — the derived type names itself as the parameter:
//
//     class Registry : public Singleton<Registry> {
//         friend class Singleton<Registry>;   // so instance() can construct it
//         Registry() = default;
//     public:
//         void remember(std::string_view key);
//     };
//
//     Registry::instance().remember("cpp_standard");
//
// The friend declaration and the private constructor are the half that matters.
// Inheriting alone only removes *copying*; it is the private constructor that
// makes `Registry local;` a compile error, and it is the friend that lets
// instance() past it. Deriving without both yields a type with a static accessor
// and no guarantee, which is worse than no pattern at all because it reads like
// one.
//
// Why CRTP rather than a `template <typename T> T& singleton()` free function:
// the free function works on any default-constructible type, which is exactly its
// problem — it makes every type a candidate and enforces nothing about the ones
// that opt in. Being a singleton is a property of the type, so the type declares
// it.
//
// Thread safety is the language's, not ours. `static Derived only;` is a
// block-scope static with dynamic initialisation, and since C++11 the first thread
// to reach it runs the constructor while every other thread blocks (GCC emits
// __cxa_guard_acquire around it). So instance() is safe to call from a ThreadPool
// worker with no lock here, and — unlike the double-checked-locking version this
// replaces in most codebases — it is safe for the right reason.
//
// Two caveats worth knowing before reaching for this, because neither is
// diagnosed:
//
//   1. Destruction order. Instances are destroyed at exit in reverse order of
//      *construction*, and that order is whatever the run took. A singleton's
//      destructor must therefore not touch another singleton. cup's own use
//      (ServiceLocator) holds shared_ptrs and releases them, which is safe under
//      any order.
//   2. It is process-wide state, so a test that mutates one is visible to the
//      next. utils answers that where it arises rather than in general: see
//      ScopedService in Service.cppm, which is the same restore-on-scope-exit
//      guard cup.platform and cup.ui already use for their installed seams.
template <typename Derived>
class Singleton {
public:
    // instance returns the one and only Derived, constructing it on first call.
    //
    // The reference outlives every caller — it refers to an object with static
    // storage duration — so callers store it as `auto&` and never as a copy. A
    // copy would not compile anyway, which is the point.
    [[nodiscard]] static Derived& instance() {
        static Derived only;
        return only;
    }

    // No copies and no moves: both would produce a second object of a type whose
    // entire contract is that there is one. Deleted rather than merely private so
    // the failure names the reason.
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

protected:
    // Protected, not public: only a derived class may construct the base, so
    // `Singleton<Registry> loose;` is rejected on its own.
    Singleton() = default;

    // Protected and non-virtual, which is the pair that matters. Non-virtual keeps
    // the type free of a vtable it has no use for; protected is what makes
    // `delete base_pointer` — the bug non-virtual destructors are famous for —
    // impossible to write rather than merely undefined.
    ~Singleton() = default;
};

}  // namespace utils
