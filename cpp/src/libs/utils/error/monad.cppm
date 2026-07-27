module;
// Light headers only — see the note in ui.cppm on what GCC 14 tolerates across the
// partitions of one module. <expected>, <optional>, <string>, <vector> are all on
// the safe list.
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>
export module utils.error:monad;

import :error;

export namespace utils::error {

// Every fallible function in cup returns std::expected<T, Error>, which already
// carries and_then / transform / transform_error / or_else. So the point of this
// partition is *not* to wrap those — call sites use them directly — but to supply
// the three joints the standard leaves out, and which a chain runs into within a
// few steps:
//
//   - require: getting into the monad from a std::optional.
//   - collect / for_each: staying in it across a range.
//
// Both range algorithms short-circuit, which is the reason they are hand-written
// rather than a <ranges> pipeline: a view has no way to abandon the range when an
// element yields an error, so the first failure has to stop the walk here.

// require lifts an optional into the expected monad, attaching the error a missing
// value should carry. It heads most chains in cup, because the thing being asked
// for first — a file's bytes, a TOML node of the right type — is an optional and
// everything after it is an expected.
//
// message is built by the caller whether or not it is needed, which a lazier
// signature (taking a callable) would avoid. That is a deliberate trade: the
// callers are a config parse and a template lookup, each run a handful of times per
// cup invocation, and a string concatenation at every call site reads better than a
// lambda at every call site.
template <typename T>
[[nodiscard]] std::expected<T, Error> require(std::optional<T> value, std::string message) {
    if (!value.has_value()) {
        return std::unexpected(Error(std::move(message)));
    }
    return *std::move(value);
}

// collect maps step over range and gathers the results, stopping at the first
// failure and reporting it. Item names what step yields, since deducing it through
// the expected costs more than it saves.
//
// The fold is written with and_then rather than an early return so the
// short-circuit is the monad's own: once the accumulator holds an error, and_then
// skips every remaining step by construction.
template <typename Item, typename Range, typename Step>
[[nodiscard]] std::expected<std::vector<Item>, Error> collect(Range&& range, Step step) {
    std::expected<std::vector<Item>, Error> gathered{std::in_place};
    for (auto&& element : std::forward<Range>(range)) {
        gathered = std::move(gathered).and_then([&step, &element](std::vector<Item> items) {
            return step(element).transform([&items](Item item) {
                items.push_back(std::move(item));
                return std::move(items);
            });
        });
    }
    return gathered;
}

// for_each runs step over every element for its effect alone, stopping at the first
// failure — collect for steps that produce nothing but an error channel.
template <typename Range, typename Step>
[[nodiscard]] std::expected<void, Error> for_each(Range&& range, Step step) {
    std::expected<void, Error> done;
    for (auto&& element : std::forward<Range>(range)) {
        done = std::move(done).and_then([&step, &element] { return step(element); });
    }
    return done;
}

}  // namespace utils::error
