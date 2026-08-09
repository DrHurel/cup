module;
#include <atomic>
#include <cassert>
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string_view>
#include <typeinfo>
#include <utility>
export module utils:singleton;

export namespace utils {

// Debug-only bookkeeping of live singletons, process-wide rather than
// per-Singleton<Type> so one call dumps everything create() has ever bound. A
// release build (NDEBUG) pays nothing for it — not even the map — the same way
// assert() above already costs nothing there; see Singleton.cpp for why the map
// itself lives in an implementation unit rather than here.
namespace singleton_registry {
void register_instance(std::type_info const& info);
void unregister_instance(std::type_info const& info);
std::size_t number_of_instances();
void dump_registry(std::ostream& stream, std::string_view separator);
}  // namespace singleton_registry

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
class Singleton {
public:
    Singleton(Singleton const&) noexcept = delete;
    Singleton(Singleton&&) noexcept = delete;
    Singleton& operator=(Singleton const&) noexcept = delete;
    Singleton& operator=(Singleton&&) noexcept = delete;

    template <typename... Args>
    static void create(Args&&... args) {
        bool expected = false;
        if (is_created_.compare_exchange_strong(expected, true)) {
            instance_.reset(new Type(std::forward<Args>(args)...));
            singleton_registry::register_instance(typeid(*instance_));
        }
    }

    static void destroy() {
        bool expected = true;
        if (is_created_.compare_exchange_strong(expected, false)) {
            singleton_registry::unregister_instance(typeid(*instance_));
            instance_.reset();
        }
    }

    // Lazily default-constructs on first call if nothing has called create() yet
    // — every current call site (ServiceLocator, and the Registry example above)
    // reaches instance() directly with no create() of its own. Explicit
    // create(args...) still works when Type takes constructor arguments: call it
    // before the first instance(), and this check finds is_created_ already true
    // and does nothing.
    static Type& instance() noexcept {
        if (!is_created_.load(std::memory_order_acquire)) {
            create();
        }
        assert(instance_ != nullptr);
        return *instance_;
    }

    static bool has_been_created() { return is_created_.load(); }

protected:
    Singleton() noexcept = default;
    ~Singleton() noexcept = default;

private:
    // Not std::unique_ptr<Type, std::default_delete<Type>>: the deleter is a
    // *different* class from Singleton<Type>, so `friend class Singleton<Type>;`
    // on Type does not extend to it — std::default_delete<Type> is never itself a
    // friend, and its operator() is instantiated wherever this inline static
    // member is first ODR-used, which can be a consumer translation unit that
    // never friended anything. There it finds Type's destructor private
    // (confirmed independently of modules: the same fails outside cup with a
    // plain header). Deleter is nested in Singleton<Type> instead: a nested class
    // is granted its enclosing class's friendships, so the one
    // `friend class Singleton<Type>;` derived types already declare is enough,
    // with no second friend declaration needed at every use site.
    struct Deleter {
        void operator()(Type* ptr) const noexcept { delete ptr; }
    };
    inline static std::unique_ptr<Type, Deleter> instance_;
    inline static std::atomic<bool> is_created_{false};
};

}  // namespace utils
