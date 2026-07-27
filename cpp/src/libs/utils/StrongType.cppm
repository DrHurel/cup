module;
// Light headers only. <string> and <string_view> are here for StrongString and the
// Viewable skill — the string case is the one cup has — and <functional> for the
// std::hash partial specialisation at the bottom of the file. Nothing heavy: this
// partition deliberately carries no <format>, <print> or <iostream>, which is why
// there is no Printable skill (see the note where the skills are defined).
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
export module utils:strong_type;

export namespace utils {

// StrongType gives a value its own type, so the compiler can tell two things apart
// that the language otherwise spells the same way:
//
//     using ProjectName = utils::StrongString<struct ProjectNameTag>;
//     using Family      = utils::StrongString<struct FamilyTag>;
//
//     std::expected<std::string, Error> render(const ProjectName& name,
//                                              const Family& family);
//
//     render(Family("modules"), ProjectName("cup"));   // no longer compiles
//
// What it is for, concretely. cup.scaffold's render() takes
// `(root, family, kind, name, vars)` — three adjacent std::string_view parameters
// naming three unrelated things — and path_to_namespace(src, dir) takes two
// std::filesystem::paths in an order nothing but the parameter name records.
// Transposing either pair is silent: it compiles, it runs, and it writes a wrong
// tree rather than producing a diagnostic. The Go original had the same hole and
// answered it the way Go does, with argument names and care. A tag type answers it
// with the type system.
//
// Nothing in cup is converted here. Phases 2 and 3 are green and rewriting green
// signatures is not parity work; this is the type Phase 4's `cup new` and `cup add`
// reach for when they start threading a name, a family and a kind through four call
// layers each. Same posture as the rest of utils.
//
// Value is what is carried, Tag is what makes it distinct, and Skills... are the
// operations it is allowed to have. The tag is never defined — `struct FamilyTag`
// declared inside the template argument list is the whole of it — because an
// incomplete type is a perfectly good name, and giving it a body would only invite
// someone to instantiate one.
//
// The three properties that make this worth having rather than a comment:
//
//   1. The constructor is explicit, so a std::string never becomes a ProjectName by
//      accident. That is the entire guarantee; an implicit constructor would leave a
//      type that documents and enforces nothing.
//   2. get() is the only way back out. There is no implicit conversion to Value —
//      one would restore exactly the transposition this prevents — so the boundary
//      where a strong type becomes a plain string is a spelled-out call.
//   3. It costs nothing. The skills are empty base classes, so
//      sizeof(StrongString<T>) == sizeof(std::string) and every operation inlines to
//      the operation on the underlying value. Pinned by a static_assert in the suite
//      rather than assumed.
//
// Two things it is not. It is not a validating type — a ProjectName holding "" or
// "../etc" is still a ProjectName, and cup.scaffold's validate_ident stays where it
// is, because a type that could only be constructed from valid input needs a
// fallible constructor and cup returns std::expected rather than throwing. And it is
// not a unit type: no dimensional analysis, no arithmetic between tags.
template <typename Value, typename Tag, template <typename> class... Skills>
class StrongType : public Skills<StrongType<Value, Tag, Skills...>>... {
public:
    // Underlying is what generic code asks for when it needs to name the carried
    // type — a function templated over "any strong string" recovers std::string from
    // it rather than re-deducing the template arguments.
    using Underlying = Value;

    // Default-constructible only when Value is, and the member's `{}` initialiser is
    // what makes `StrongType<int, Tag> n;` a zero rather than whatever was in that
    // memory. Constrained rather than left to be implicitly deleted, so the failure
    // names the requirement that was missed.
    //
    // Worth having at all because cup's configuration structs are aggregates with
    // defaulted members — cup.project's Config is the shape this has to fit.
    StrongType()
        requires std::default_initializable<Value>
    = default;

    // Explicit, both of them. See property 1 above: this is the guarantee.
    explicit constexpr StrongType(const Value& value) noexcept(
        std::is_nothrow_copy_constructible_v<Value>)
        : value_(value) {}
    explicit constexpr StrongType(Value&& value) noexcept(
        std::is_nothrow_move_constructible_v<Value>)
        : value_(std::move(value)) {}

    // get() is the one door back to the value.
    //
    // Three overloads rather than one, and the rvalue overload is the one that earns
    // its place: `std::move(name).get()` hands the std::string out by move instead of
    // copying it, which is what a function assembling a path out of three names
    // wants. The non-const lvalue overload allows mutation in place — a hole, in the
    // sense that anything at all can be assigned through it, but a deliberate one:
    // the alternative is a copy per edit, and `name.get() = other` is loud enough to
    // find in review.
    [[nodiscard]] constexpr const Value& get() const& noexcept { return value_; }
    [[nodiscard]] constexpr Value& get() & noexcept { return value_; }
    [[nodiscard]] constexpr Value&& get() && noexcept { return std::move(value_); }

private:
    Value value_{};
};

// --- Skills ------------------------------------------------------------------
//
// A skill is a CRTP mixin naming one operation the strong type is allowed to have,
// and the list is opt-in because that is the half of the pattern that does the work:
// a type carrying every operation its underlying value has is a typedef with extra
// syntax. Ordering two ProjectNames is meaningful — a sorted list of libraries is
// something cup prints — and ordering two rendered file *contents* is not, so the
// second type does not get `<`.
//
// Each operation is a hidden friend defined in the mixin, so it is found by
// argument-dependent lookup on the strong type and by nothing else. That matters
// more than it looks: a free `operator==` template over StrongType would be a
// candidate for every comparison in every translation unit that imports utils.
//
// Adding one is three lines, and the two omissions are deliberate rather than
// pending:
//
//   * No Printable / Formattable / Streamable. Rule 1 of the port is that at most one
//     partition of a module may reach <format>, <print> or <iostream>, and utils has
//     spent that budget on nothing so far — which is worth keeping.
//     `std::println("{}", name.get())` costs one call and no BMI.
//   * No Addable / Incrementable / Multipliable. cup's candidate strong types are
//     names, families, kinds and standards; adding two C++ standards together is not
//     an operation anyone wants, and a skill nothing uses is a skill nobody
//     maintains.

// Equatable gives == (and, by C++20 rewriting, !=), delegating to the underlying
// value's own comparison.
template <typename Self>
struct Equatable {
    [[nodiscard]] friend constexpr bool operator==(const Self& a, const Self& b) {
        return a.get() == b.get();
    }
};

// Ordered gives <=> — and therefore <, <=, > and >= — on top of Equatable's ==.
//
// It derives from Equatable rather than redeclaring ==, because a type with an
// ordering and no equality is not one anyone can use, and because listing both
// skills then stays harmless: Equatable is one class either way, whether it is
// reached directly, through Ordered, or both.
//
// The return type is deduced, so it is whatever the underlying comparison yields —
// std::strong_ordering for std::string and int, std::partial_ordering for a double.
template <typename Self>
struct Ordered : Equatable<Self> {
    [[nodiscard]] friend constexpr auto operator<=>(const Self& a, const Self& b) {
        return a.get() <=> b.get();
    }
};

// Hashable is a marker: it declares nothing, and exists to be detected by the
// std::hash partial specialisation at the bottom of this file — which is what makes
// a strong type usable as an unordered_map or unordered_set key.
//
// Opt-in like the rest, and for the same reason. Note the asymmetry, since it is the
// one that bites: a key type needs *both* a hash and an equality, so a type with
// Hashable and no Equatable fails at the container rather than at the declaration.
// Listing Ordered alongside it, as StrongString does, is the shape that always
// works.
template <typename Self>
struct Hashable {};

// Viewable gives an implicit conversion to std::string_view, for a strong type over
// something string-shaped.
//
// This is the one skill that gives back a little of what the pattern took away, and
// it is here because of what cup's interfaces look like: nearly every function under
// cup/ takes a std::string_view, so without it a strong string would be spelled
// `.get()` at each of the several hundred call sites the port already has. The
// conversion is one-way — a std::string_view does not become a ProjectName — so the
// guarantee that matters survives it.
//
// Two things to know. A string_view does not own, so
// `std::string_view v = ProjectName("cup");` dangles the moment the temporary dies —
// the same hazard as viewing any temporary string, no worse. And comparing against a
// literal still needs the value: `name == std::string_view("cup")` resolves,
// `name == "cup"` does not, because argument-dependent lookup on a StrongType and a
// const char* reaches no operator== at all.
template <typename Self>
struct Viewable {
    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return std::string_view(static_cast<const Self&>(*this).get());
    }
};

// --- The string case ---------------------------------------------------------

// StrongString is the alias for the type cup would actually declare:
//
//     using ProjectName = utils::StrongString<struct ProjectNameTag>;
//
// A std::string with the three skills a name wants — ordered (so a std::map or a
// std::sort of them works, which is what cup.tmpl's sorted listings need), hashable
// (so an unordered_map does), and viewable (so it reaches the std::string_view
// interfaces under cup/ without a .get() at every call).
//
// Spelling the underlying type std::string rather than std::string_view is
// deliberate: a strong type is stored — in a Config, in a Vars map, in a struct
// threaded through four call layers — and a stored view is a dangling reference
// waiting for its owner to be reassigned. Take the copy; a project name is
// scaffolded once per run.
template <typename Tag>
using StrongString = StrongType<std::string, Tag, Ordered, Hashable, Viewable>;

}  // namespace utils

// std::hash for any StrongType that asked for Hashable, forwarding to the underlying
// value's own hash.
//
// Constrained on the marker rather than declared for every StrongType, so opting out
// stays expressible: a type without Hashable has no hash at all, and using it as an
// unordered_map key fails at the declaration instead of silently succeeding.
//
// On why this is allowed to live in a module at all: std::hash comes from the global
// module fragment, so it is attached to the global module — and a specialisation of a
// template attached to the global module is attached to the global module too, not to
// `utils`. That is what makes it findable from a consumer, and it is why this one
// declaration sits outside the `export namespace utils` block above: specialisations
// are not exported, they are found.
namespace std {

template <typename Value, typename Tag, template <typename> class... Skills>
    requires std::derived_from<utils::StrongType<Value, Tag, Skills...>,
                               utils::Hashable<utils::StrongType<Value, Tag, Skills...>>>
struct hash<utils::StrongType<Value, Tag, Skills...>> {
    [[nodiscard]] std::size_t operator()(
        const utils::StrongType<Value, Tag, Skills...>& value) const {
        return std::hash<Value>{}(value.get());
    }
};

}  // namespace std
