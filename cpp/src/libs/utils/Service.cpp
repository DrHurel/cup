// Implementation unit for utils:service — the locator's map and the lock that
// guards it.
//
// This is a module implementation unit (`module utils;`, no `export`), so its
// global module fragment never becomes part of any BMI. Two things follow, and the
// second is the one that made this file necessary rather than merely tidy:
//
//   1. <unordered_map>, <typeindex> and <mutex> are compiled here once and seen by
//      nothing else — the same reason <curl/curl.h> lives in Http.cpp.
//   2. std::unordered_map is instantiated *here*, in an ordinary translation unit,
//      rather than in whatever consumer instantiates ServiceLocator's templates.
//      On GCC 14 the latter does not compile at all; see the note on store() in
//      Service.cppm for the diagnostic.
module;
#include <cstddef>
#include <memory>
#include <mutex>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
module utils;

namespace utils {

struct ServiceLocator::State {
    // Bindings are written at startup and read from anywhere, including ThreadPool
    // workers, so every access takes the lock. It is uncontended in practice — the
    // reads are a hash lookup and a shared_ptr copy — which is why this is a plain
    // mutex and not a shared_mutex: reader/writer locking costs more in the
    // uncontended case than it saves here, and the honest fix for a hot path is to
    // resolve the service once and hold the shared_ptr rather than to ask again.
    //
    // mutable so the const accessors can lock it. The lock protects the map, not
    // the logical constness of a lookup.
    mutable std::mutex mutex;

    // std::shared_ptr<void> keeps the deleter the binding was created with, so the
    // erased object is destroyed as its concrete type without this map knowing
    // what that type was. The pointer stored is already the *interface* pointer —
    // register_instance does the upcast before erasing — so the cast back on the
    // way out is a plain static_pointer_cast and lands on the right subobject.
    std::unordered_map<std::type_index, std::shared_ptr<void>> services;
};

ServiceLocator::ServiceLocator() : state_(std::make_unique<State>()) {}

// Declared in Service.cppm and defined here rather than defaulted there, because
// State is incomplete in the interface unit.
//
// It runs at exit, on the one instance Singleton holds. All it does is release the
// shared_ptrs — which is what makes this singleton safe under the destruction
// order caveat documented in Singleton.cppm: it touches no other singleton, only
// the objects it owns.
ServiceLocator::~ServiceLocator() = default;

void ServiceLocator::store(const std::type_info& interface, std::shared_ptr<void> service) {
    const std::lock_guard guard(state_->mutex);
    state_->services.insert_or_assign(std::type_index(interface), std::move(service));
}

std::shared_ptr<void> ServiceLocator::lookup(const std::type_info& interface) const {
    const std::lock_guard guard(state_->mutex);
    const auto found = state_->services.find(std::type_index(interface));
    if (found == state_->services.end()) {
        return nullptr;
    }
    return found->second;
}

bool ServiceLocator::bound(const std::type_info& interface) const {
    const std::lock_guard guard(state_->mutex);
    return state_->services.contains(std::type_index(interface));
}

bool ServiceLocator::drop(const std::type_info& interface) {
    const std::lock_guard guard(state_->mutex);
    return state_->services.erase(std::type_index(interface)) != 0;
}

void ServiceLocator::clear() {
    // The map is emptied under the lock, but the services it held are destroyed
    // *after* it is released: dropping the last reference to a service runs that
    // service's destructor, and a destructor that resolves something — a logger
    // writing a farewell line — would deadlock on a non-recursive mutex held here.
    // Moving the map out first makes the destruction happen at the end of this
    // scope, unlocked.
    std::unordered_map<std::type_index, std::shared_ptr<void>> discarded;
    {
        const std::lock_guard guard(state_->mutex);
        discarded.swap(state_->services);
    }
}

std::size_t ServiceLocator::size() const {
    const std::lock_guard guard(state_->mutex);
    return state_->services.size();
}

}  // namespace utils
