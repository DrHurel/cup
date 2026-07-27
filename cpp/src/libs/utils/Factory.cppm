module;
// Light headers plus <memory>, which :service and :pool also take. See the note in
// Service.cppm on why three partitions sharing it merges here.
#include <concepts>
#include <cstddef>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
export module utils:factory;

// Re-exported: utils::error::Error is the E of create()'s result.
export import utils.error;

export namespace utils {

// Factory turns a run-time key into an object of a compile-time-unknown type:
//
//     Factory<Renderer> renderers;
//     renderers.register_type<ModulesRenderer>("modules");
//     renderers.register_type<HeadersRenderer>("headers");
//     ...
//     auto renderer = renderers.create(config.family());
//
// Product is the interface the objects share, Key is what selects one (std::string
// unless told otherwise), and Args... is the argument list every implementation's
// constructor takes. Fixing that list in the class template rather than deducing
// it per call is what lets a creator be a plain function pointer — see below —
// and it is honest about the fact that a factory whose products need different
// constructor arguments is not a factory.
//
// This is the pattern cup's own dispatch keeps open-coding. `scaffold::render`
// selects a template family with a chain of comparisons, and `cup add` selects a
// source shape with another; both are closed sets today, and both are exactly the
// switch a factory replaces the moment a project's `.cup/templates/` override
// wants to add a family cup does not ship. Nothing is converted here — Phase 3 is
// green and rewriting green dispatch is not parity work — but this is where that
// conversion lands.
//
// A Factory is a value: construct one, own it, pass it by reference. Where a
// process wants exactly one, that is what :service is for — bind it as a service
// rather than deriving it from Singleton, so the type stays testable.
template <typename Product, typename Key = std::string, typename... Args>
class Factory {
public:
    // Creator is a plain function pointer, not a std::function.
    //
    // Partly a house rule: instantiating std::function over a signature involving
    // std::expected of a non-trivial type is what broke cup.platform on GCC 14
    // (see the note on HttpGet in Http.cppm). Mostly it is the right type anyway —
    // a creator that captures state is a creator whose products depend on when it
    // was registered, which is a bug wearing a pattern's clothes. Everything
    // register_type() installs is a captureless lambda, which converts.
    using Creator = std::unique_ptr<Product> (*)(Args...);

    // register_type binds key to "make an Impl". Returns false and changes nothing
    // if key is already registered.
    //
    // Rejecting rather than replacing is the deliberate half. A factory is
    // populated once, at startup, from a fixed list; a second registration under
    // one key means two subsystems both believed they owned it, and silently
    // letting the later one win turns that into a behaviour that depends on
    // static initialisation order. Callers that genuinely want to replace one say
    // so: unregister() then register_type().
    template <std::derived_from<Product> Impl>
        requires std::constructible_from<Impl, Args...>
    bool register_type(Key key) {
        // create() hands back a unique_ptr<Product> that owns an Impl, so deleting
        // it through the base is the *only* way it is ever destroyed. Without a
        // virtual destructor on Product that is undefined behaviour which happens
        // to work whenever Impl adds no members — i.e. it compiles, passes, and
        // starts leaking the day someone adds a std::string to a product. Caught
        // here, where the mistake is, rather than at the delete.
        static_assert(std::same_as<Impl, Product> || std::has_virtual_destructor_v<Product>,
                      "Factory<Product>: Product needs a virtual destructor, because "
                      "create() destroys derived products through a unique_ptr<Product>");
        return register_creator(std::move(key), [](Args... args) -> std::unique_ptr<Product> {
            return std::make_unique<Impl>(std::forward<Args>(args)...);
        });
    }

    // register_creator binds key to an arbitrary creator — for a product that is
    // not simply `new Impl(args...)`: one that comes from a pool, or that picks
    // its own subtype from the arguments.
    bool register_creator(Key key, Creator creator) {
        if (creator == nullptr) {
            return false;
        }
        return creators_.try_emplace(std::move(key), creator).second;
    }

    // create builds the product registered under key, or returns an Error naming
    // the key that was not registered.
    //
    // The result is std::unique_ptr, not shared: a freshly built object has one
    // owner by definition, and a caller that wants to share it can say so with a
    // single conversion. The reverse is not available.
    [[nodiscard]] std::expected<std::unique_ptr<Product>, error::Error> create(
        const Key& key, Args... args) const {
        const auto found = creators_.find(key);
        if (found == creators_.end()) {
            return std::unexpected(error::Error(unknown_key_message(key)));
        }
        return found->second(std::forward<Args>(args)...);
    }

    // contains reports whether key is registered — the question to ask before
    // create() when a miss is expected rather than exceptional.
    [[nodiscard]] bool contains(const Key& key) const { return creators_.contains(key); }

    // keys lists every registered key in sorted order. cup prints these: they are
    // what a `--help` enumerates and what an "unknown family" message suggests, and
    // both must be stable between runs. That ordering is why the store is a
    // std::map and not an unordered_map — the same call cup.tmpl's directory
    // listing makes with std::set, for the same reason.
    [[nodiscard]] std::vector<Key> keys() const {
        std::vector<Key> names;
        names.reserve(creators_.size());
        for (const auto& entry : creators_) {
            names.push_back(entry.first);
        }
        return names;
    }

    // unregister drops key and reports whether it was there.
    bool unregister(const Key& key) { return creators_.erase(key) != 0; }

    // clear drops every registration.
    void clear() { creators_.clear(); }

    // size is the number of registered keys.
    [[nodiscard]] std::size_t size() const noexcept { return creators_.size(); }

private:
    // unknown_key_message names the key when the key type can be printed and stays
    // generic when it cannot.
    //
    // A Key of std::string or std::string_view — every use cup has — produces
    // `no creator registered for "modules"`, quoted the way cup.scaffold quotes a
    // rejected standard. A Key of some enum produces the same sentence without the
    // name, because rendering an arbitrary key would mean requiring every key type
    // to be formattable, and that is a large tax on the common case to improve a
    // message in the rare one.
    [[nodiscard]] static std::string unknown_key_message(const Key& key) {
        if constexpr (std::convertible_to<const Key&, std::string_view>) {
            return std::string("no creator registered for \"") +
                   std::string(std::string_view(key)) + "\"";
        } else {
            return "no creator registered for the requested key";
        }
    }

    std::map<Key, Creator> creators_;
};

}  // namespace utils
