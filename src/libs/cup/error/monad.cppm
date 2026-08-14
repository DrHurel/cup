module;
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>
export module cup.error:monad;

import :error;

export namespace cup::error {

template <typename T>
[[nodiscard]] std::expected<T, Error> require(std::optional<T> value, std::string message) {
    if (!value.has_value()) {
        return std::unexpected(Error(std::move(message)));
    }
    return *std::move(value);
}

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

template <typename Range, typename Step>
[[nodiscard]] std::expected<void, Error> for_each(Range&& range, Step step) {
    std::expected<void, Error> done;
    for (auto&& element : std::forward<Range>(range)) {
        done = std::move(done).and_then([&step, &element] { return step(element); });
    }
    return done;
}

}
