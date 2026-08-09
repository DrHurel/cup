// Implementation unit for utils:singleton — the debug-only registry of live
// singletons.
//
// This is a module implementation unit (`module utils;`, no `export`), so its
// global module fragment never becomes part of any BMI. <unordered_map> and
// <ostream> are compiled here once and seen by nothing else, the same move
// Service.cpp makes for its own map and the same reason: instantiating
// std::unordered_map from a template an interface unit exports fails on GCC 14
// in the *consumer* (see the note on store() in Service.cppm and rule 6 in
// docs/migration-cpp23.md). register_instance()/unregister_instance()/
// number_of_instances() are not templates — they take a std::type_info const&,
// not a Type — so nothing here is instantiated per Singleton<Type> at all; this
// split is purely about keeping the interface partition's header budget light.
module;
#include <cassert>
#include <cstddef>
#include <ostream>
#include <string_view>
#include <typeinfo>
#include <unordered_map>
module utils;

namespace utils::singleton_registry {

namespace {
#ifdef NDEBUG
constexpr bool kTrackInstances = false;
#else
constexpr bool kTrackInstances = true;
#endif

// Keyed on the type's hash rather than the type_info object itself: typeid may
// hand back a different std::type_info instance for the same type across
// translation units, but hash_code() is stable for the process's lifetime.
std::unordered_map<std::size_t, const char*> registry;
}  // namespace

void register_instance(std::type_info const& info) {
    if constexpr (kTrackInstances) {
        auto const hash = info.hash_code();
        assert(!registry.contains(hash));
        registry.try_emplace(hash, info.name());
    }
}

void unregister_instance(std::type_info const& info) {
    if constexpr (kTrackInstances) {
        auto const hash = info.hash_code();
        assert(registry.contains(hash));
        registry.erase(hash);
    }
}

std::size_t number_of_instances() {
    if constexpr (kTrackInstances) {
        return registry.size();
    } else {
        return 0;
    }
}

void dump_registry(std::ostream& stream, std::string_view separator) {
    for (auto const& entry : registry) {
        stream << separator << entry.second << "\n";
    }
}

}  // namespace utils::singleton_registry
