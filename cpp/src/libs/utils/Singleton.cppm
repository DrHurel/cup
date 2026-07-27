module;
#include <atomic>
#include <cassert>
#include <memory>
#include <utility>
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
template <typename Type>
    class Singleton
    {
    private:
        inline static std::unique_ptr<Type> instance_;
        inline static std::atomic<bool> is_created_{false};

    protected:
        Singleton() noexcept = default;
        ~Singleton() noexcept = default;

    public:
        Singleton(Singleton const &) noexcept = delete;
        Singleton(Singleton &&) noexcept = delete;
        Singleton &operator=(Singleton const &) noexcept = delete;
        Singleton &operator=(Singleton &&) noexcept = delete;
        template <typename... Args>
        static void create(Args &&...args)
        {
            bool expected = false;
            if (is_created_.compare_exchange_strong(expected, true))
            {
                instance_.reset(new Type(std::forward<Args>(args)...));
            }
        }

        static void destroy()
        {
            bool expected = true;
            if (is_created_.compare_exchange_strong(expected, false))
            {
                instance_.reset();
            }
        }

        static Type &instance() noexcept
        {
            assert(instance_ != nullptr);
            return *instance_.get();
        }

        static bool has_been_created() { return is_created_.load(); }
    };
}  // namespace utils
