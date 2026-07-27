// Suite for utils — the four object-lifetime patterns the rest of cup builds
// on.
//
// Unlike the Phase 2 and Phase 3 suites, this one ports no Go test: there is no Go
// counterpart to port. Go answers all four of these with a package-level variable
// and an init(), so what is checked here is the *guarantee* each pattern claims,
// and in particular the handful of claims that are easy to write and easy to get
// silently wrong — the base-pointer offset a service locator's type erasure has to
// preserve, the future a thread pool's destructor must not break, and the
// restore-on-scope-exit that keeps a process-wide singleton from leaking between
// two of these cases.

#include <catch2/catch_test_macros.hpp>

// A module re-exports no declaration from its global module fragment, so a
// consumer naming std::expected, std::string or std::future includes them itself.
// (Same rule as the <functional> note in ui_test.cpp.)
#include <atomic>
#include <expected>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

import utils;

namespace {

using utils::Factory;
using utils::ScopedService;
using utils::ServiceLocator;
using utils::Singleton;
using utils::ThreadPool;

// --- Fixtures for :singleton -------------------------------------------------

// Counter is the canonical shape the pattern documents: private constructor plus
// the friend declaration that lets instance() past it. Without both, deriving from
// Singleton removes copying and guarantees nothing.
class Counter : public Singleton<Counter> {
public:
    void bump() { ++value_; }
    [[nodiscard]] int value() const { return value_; }

private:
    friend class Singleton<Counter>;
    Counter() = default;

    int value_ = 0;
};

// A second singleton type, to confirm the instance is per-type rather than shared
// across everything deriving from the template.
class Label : public Singleton<Label> {
public:
    void set(std::string text) { text_ = std::move(text); }
    [[nodiscard]] const std::string& get() const { return text_; }

private:
    friend class Singleton<Label>;
    Label() = default;

    std::string text_;
};

// --- Fixtures for :service ---------------------------------------------------

class Greeter {
public:
    Greeter() = default;
    Greeter(const Greeter&) = delete;
    Greeter& operator=(const Greeter&) = delete;
    virtual ~Greeter() = default;
    [[nodiscard]] virtual std::string greet() const = 0;
};

class EnglishGreeter : public Greeter {
public:
    [[nodiscard]] std::string greet() const override { return "hello"; }
};

class FrenchGreeter : public Greeter {
public:
    [[nodiscard]] std::string greet() const override { return "bonjour"; }
};

// Takes a constructor argument, so register_service's forwarding is exercised
// rather than assumed.
class NamedGreeter : public Greeter {
public:
    explicit NamedGreeter(std::string name) : name_(std::move(name)) {}
    [[nodiscard]] std::string greet() const override { return "hello, " + name_; }

private:
    std::string name_;
};

// A second, unrelated interface. Its first member is deliberately non-empty so an
// implementation deriving from both puts the two base subobjects at *different*
// offsets — which is the case that catches erasing the wrong pointer.
class Counterparty {
public:
    Counterparty() = default;
    Counterparty(const Counterparty&) = delete;
    Counterparty& operator=(const Counterparty&) = delete;
    virtual ~Counterparty() = default;
    [[nodiscard]] virtual int identifier() const = 0;

private:
    // NOLINTNEXTLINE(*-unused-private-field) — present to give the base a size.
    long long padding_ = 0;
};

class BilingualGreeter : public Greeter, public Counterparty {
public:
    [[nodiscard]] std::string greet() const override { return "hello / bonjour"; }
    [[nodiscard]] int identifier() const override { return 42; }
};

// --- Fixtures for :factory ---------------------------------------------------

class Shape {
public:
    Shape() = default;
    Shape(const Shape&) = delete;
    Shape& operator=(const Shape&) = delete;
    virtual ~Shape() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual int sides() const = 0;
};

class Square : public Shape {
public:
    [[nodiscard]] std::string name() const override { return "square"; }
    [[nodiscard]] int sides() const override { return 4; }
};

class Triangle : public Shape {
public:
    [[nodiscard]] std::string name() const override { return "triangle"; }
    [[nodiscard]] int sides() const override { return 3; }
};

// Constructed from an int, for the factory whose Args... is non-empty.
class Polygon : public Shape {
public:
    explicit Polygon(int sides) : sides_(sides) {}
    [[nodiscard]] std::string name() const override { return "polygon"; }
    [[nodiscard]] int sides() const override { return sides_; }

private:
    int sides_;
};

enum class Family { Modules, Headers };

}  // namespace

// =============================================================================
// :singleton
// =============================================================================

TEST_CASE("Singleton hands back one instance per type", "[utils][singleton]") {
    Counter& first = Counter::instance();
    Counter& second = Counter::instance();
    REQUIRE(&first == &second);

    const int before = first.value();
    first.bump();
    // Observed through the other reference: the two name the same object, so this
    // is what "one instance" means operationally.
    REQUIRE(second.value() == before + 1);

    // Per type, not per template: Label's instance is its own.
    Label::instance().set("cup");
    REQUIRE(Label::instance().get() == "cup");
    REQUIRE(static_cast<void*>(&Counter::instance()) != static_cast<void*>(&Label::instance()));
}

TEST_CASE("Singleton forbids a second instance", "[utils][singleton]") {
    // The guarantee is a compile-time one, so these are the assertions. A private
    // constructor is what makes `Counter local;` ill-formed; the deleted copy and
    // move are what stop a second object being made from the first.
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Counter>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Counter>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Counter>);
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Counter>);

    // Non-virtual destructor: the type carries no vtable it has no use for, and
    // the protected access is what makes deleting through the base unwritable.
    STATIC_REQUIRE_FALSE(std::has_virtual_destructor_v<Counter>);
}

// =============================================================================
// :service
// =============================================================================

TEST_CASE("ServiceLocator binds an implementation to an interface", "[utils][service]") {
    ServiceLocator& services = ServiceLocator::instance();
    services.clear();

    REQUIRE_FALSE(services.contains<Greeter>());
    REQUIRE(services.size() == 0);

    services.register_service<Greeter, EnglishGreeter>();

    REQUIRE(services.contains<Greeter>());
    REQUIRE(services.size() == 1);

    const auto greeter = services.get<Greeter>();
    REQUIRE(greeter.has_value());
    // Resolved through the interface, dispatched to the implementation — the whole
    // transaction the locator exists for.
    REQUIRE((*greeter)->greet() == "hello");

    services.clear();
    REQUIRE_FALSE(services.contains<Greeter>());
}

TEST_CASE("ServiceLocator forwards constructor arguments", "[utils][service]") {
    ServiceLocator& services = ServiceLocator::instance();
    services.clear();

    // The returned pointer is the concrete type, not the interface, so a caller can
    // keep configuring what it just registered.
    const std::shared_ptr<NamedGreeter> made =
        services.register_service<Greeter, NamedGreeter>(std::string("cup"));
    REQUIRE(made != nullptr);
    REQUIRE(made->greet() == "hello, cup");

    const auto resolved = services.get<Greeter>();
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->get() == made.get());

    services.clear();
}

TEST_CASE("ServiceLocator reports an unbound interface as an error", "[utils][service]") {
    ServiceLocator& services = ServiceLocator::instance();
    services.clear();

    const auto missing = services.get<Greeter>();
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().message().contains("no service registered for"));

    // find() is the same lookup without the error channel.
    REQUIRE(services.find<Greeter>() == nullptr);
}

TEST_CASE("ServiceLocator preserves the base offset through type erasure",
          "[utils][service]") {
    // The case the shared_ptr<void> store is most likely to get wrong. A
    // BilingualGreeter's Greeter and Counterparty subobjects sit at different
    // addresses, so erasing the *derived* pointer and casting it back to
    // Counterparty would hand out an address off by the size of the Greeter
    // subobject — which is not a crash, it is a silently corrupt object. Erasing
    // the already-upcast interface pointer is what makes both of these hold.
    ServiceLocator& services = ServiceLocator::instance();
    services.clear();

    const auto impl = std::make_shared<BilingualGreeter>();
    services.register_instance<Greeter>(impl);
    services.register_instance<Counterparty>(impl);

    const auto as_greeter = services.get<Greeter>();
    const auto as_counterparty = services.get<Counterparty>();
    REQUIRE(as_greeter.has_value());
    REQUIRE(as_counterparty.has_value());

    REQUIRE((*as_greeter)->greet() == "hello / bonjour");
    REQUIRE((*as_counterparty)->identifier() == 42);

    // Both resolve to the one object, at their own correctly-adjusted addresses.
    REQUIRE(dynamic_cast<BilingualGreeter*>(as_greeter->get()) == impl.get());
    REQUIRE(dynamic_cast<BilingualGreeter*>(as_counterparty->get()) == impl.get());

    services.clear();
}

TEST_CASE("ServiceLocator rebinds and unbinds", "[utils][service]") {
    ServiceLocator& services = ServiceLocator::instance();
    services.clear();

    services.register_service<Greeter, EnglishGreeter>();
    const auto english = services.get<Greeter>();
    REQUIRE(english.has_value());

    services.register_service<Greeter, FrenchGreeter>();
    const auto french = services.get<Greeter>();
    REQUIRE(french.has_value());
    REQUIRE((*french)->greet() == "bonjour");

    // Rebinding drops the locator's reference but not the caller's: the shared_ptr
    // handed out earlier still names a live object. That is what makes
    // ScopedService's restore safe rather than a use-after-free.
    REQUIRE((*english)->greet() == "hello");
    REQUIRE(services.size() == 1);

    REQUIRE(services.unbind<Greeter>());
    REQUIRE_FALSE(services.unbind<Greeter>());
    REQUIRE_FALSE(services.contains<Greeter>());
}

TEST_CASE("ScopedService restores the previous binding", "[utils][service]") {
    ServiceLocator& services = ServiceLocator::instance();
    services.clear();

    services.register_service<Greeter, EnglishGreeter>();

    {
        const ScopedService<Greeter> guard(std::make_shared<FrenchGreeter>());
        const auto during = services.get<Greeter>();
        REQUIRE(during.has_value());
        REQUIRE((*during)->greet() == "bonjour");
    }

    const auto after = services.get<Greeter>();
    REQUIRE(after.has_value());
    REQUIRE((*after)->greet() == "hello");

    services.clear();
}

TEST_CASE("ScopedService unbinds when there was nothing to restore", "[utils][service]") {
    ServiceLocator& services = ServiceLocator::instance();
    services.clear();

    {
        const ScopedService<Greeter> guard(std::make_shared<EnglishGreeter>());
        REQUIRE(services.contains<Greeter>());
    }

    // "Previously nothing" has to restore to nothing, or the first suite to install
    // a stub leaves it installed for every case after it.
    REQUIRE_FALSE(services.contains<Greeter>());
}

// =============================================================================
// :factory
// =============================================================================

TEST_CASE("Factory builds the type registered under a key", "[utils][factory]") {
    Factory<Shape> shapes;
    REQUIRE(shapes.size() == 0);

    REQUIRE(shapes.register_type<Square>("square"));
    REQUIRE(shapes.register_type<Triangle>("triangle"));
    REQUIRE(shapes.size() == 2);
    REQUIRE(shapes.contains("square"));
    REQUIRE_FALSE(shapes.contains("circle"));

    const auto square = shapes.create("square");
    REQUIRE(square.has_value());
    REQUIRE((*square)->name() == "square");
    REQUIRE((*square)->sides() == 4);

    const auto triangle = shapes.create("triangle");
    REQUIRE(triangle.has_value());
    REQUIRE((*triangle)->sides() == 3);

    // Each call builds a new product — a factory, not a registry of instances.
    const auto another = shapes.create("square");
    REQUIRE(another.has_value());
    REQUIRE(another->get() != square->get());
}

TEST_CASE("Factory refuses to replace a registered key", "[utils][factory]") {
    Factory<Shape> shapes;
    REQUIRE(shapes.register_type<Square>("shape"));

    // Rejected rather than silently overwritten: two subsystems both claiming one
    // key is a wiring bug, and letting the later one win makes the behaviour depend
    // on initialisation order.
    REQUIRE_FALSE(shapes.register_type<Triangle>("shape"));

    const auto made = shapes.create("shape");
    REQUIRE(made.has_value());
    REQUIRE((*made)->name() == "square");

    // Replacing is available, but has to be asked for.
    REQUIRE(shapes.unregister("shape"));
    REQUIRE(shapes.register_type<Triangle>("shape"));
    const auto replaced = shapes.create("shape");
    REQUIRE(replaced.has_value());
    REQUIRE((*replaced)->name() == "triangle");
}

TEST_CASE("Factory names the key it does not know", "[utils][factory]") {
    Factory<Shape> shapes;
    REQUIRE(shapes.register_type<Square>("square"));

    const auto missing = shapes.create("hexagon");
    REQUIRE_FALSE(missing.has_value());
    // Quoted the way cup.scaffold quotes a standard it rejected.
    REQUIRE(missing.error().message().contains("\"hexagon\""));
}

TEST_CASE("Factory lists its keys in sorted order", "[utils][factory]") {
    Factory<Shape> shapes;
    REQUIRE(shapes.register_type<Triangle>("triangle"));
    REQUIRE(shapes.register_type<Square>("square"));
    REQUIRE(shapes.register_type<Square>("box"));

    // cup prints these — a `--help` enumeration, an "unknown family" suggestion —
    // so the order has to be stable between runs, not whatever a hash produced.
    const std::vector<std::string> want{"box", "square", "triangle"};
    REQUIRE(shapes.keys() == want);

    shapes.clear();
    REQUIRE(shapes.size() == 0);
    REQUIRE(shapes.keys().empty());
}

TEST_CASE("Factory forwards constructor arguments to the product", "[utils][factory]") {
    // Args... is fixed by the class template, so every product takes the same
    // arguments — which is what lets a creator stay a plain function pointer.
    Factory<Shape, std::string, int> polygons;
    REQUIRE(polygons.register_type<Polygon>("polygon"));

    const auto seven = polygons.create("polygon", 7);
    REQUIRE(seven.has_value());
    REQUIRE((*seven)->sides() == 7);
}

TEST_CASE("Factory accepts a non-string key", "[utils][factory]") {
    Factory<Shape, Family> families;
    REQUIRE(families.register_type<Square>(Family::Modules));

    const auto modules = families.create(Family::Modules);
    REQUIRE(modules.has_value());
    REQUIRE((*modules)->name() == "square");

    // A key that cannot be turned into a string yields the generic message rather
    // than requiring every key type to be formattable.
    const auto headers = families.create(Family::Headers);
    REQUIRE_FALSE(headers.has_value());
    REQUIRE(headers.error().message().contains("no creator registered"));
}

TEST_CASE("Factory takes a creator that is not just a constructor", "[utils][factory]") {
    Factory<Shape> shapes;
    // The escape hatch for a product that is not `new Impl(args...)` — here, one
    // that picks its own subtype.
    REQUIRE(shapes.register_creator("even", [] -> std::unique_ptr<Shape> {
        return std::make_unique<Square>();
    }));
    REQUIRE_FALSE(shapes.register_creator("null", nullptr));

    const auto made = shapes.create("even");
    REQUIRE(made.has_value());
    REQUIRE((*made)->sides() == 4);
    REQUIRE_FALSE(shapes.contains("null"));
}

// =============================================================================
// :pool
// =============================================================================

TEST_CASE("ThreadPool returns a task's value through its future", "[utils][pool]") {
    ThreadPool pool(2);
    REQUIRE(pool.size() == 2);

    auto sum = pool.submit([](int a, int b) { return a + b; }, 2, 3);
    REQUIRE(sum.get() == 5);

    // A void task has a future too — the way to know it finished.
    std::atomic<bool> ran{false};
    auto done = pool.submit([&ran] { ran.store(true); });
    done.get();
    REQUIRE(ran.load());
}

TEST_CASE("ThreadPool sizes itself when asked for no particular count", "[utils][pool]") {
    // hardware_concurrency() is allowed to return 0, and a pool of zero threads
    // would accept work that never runs — a hang rather than an error.
    const ThreadPool pool;
    REQUIRE(pool.size() >= 1);
}

TEST_CASE("ThreadPool runs every task it is given", "[utils][pool]") {
    constexpr int kTasks = 200;
    std::atomic<int> completed{0};

    {
        ThreadPool pool(4);
        for (int i = 0; i < kTasks; ++i) {
            static_cast<void>(pool.submit([&completed] { completed.fetch_add(1); }));
        }
        // Deterministic rather than timed: wait_idle returns when the queue is
        // empty *and* nothing is in flight.
        pool.wait_idle();
        REQUIRE(completed.load() == kTasks);
        REQUIRE(pool.pending() == 0);
    }

    REQUIRE(completed.load() == kTasks);
}

TEST_CASE("ThreadPool copies a task's arguments", "[utils][pool]") {
    ThreadPool pool(1);

    // Copied at submit(), like std::async and std::thread, because the call happens
    // later on another thread and a reference to this frame may be gone by then.
    std::string value = "before";
    auto echoed = pool.submit([](std::string text) { return text; }, value);
    value = "after";

    REQUIRE(echoed.get() == "before");
}

TEST_CASE("ThreadPool surfaces a task's exception through its future", "[utils][pool]") {
    ThreadPool pool(1);

    auto thrown = pool.submit([]() -> int { throw std::runtime_error("task failed"); });

    // Caught by the packaged_task and stored, not left to terminate a worker.
    REQUIRE_THROWS_AS(thrown.get(), std::runtime_error);

    // The worker survived it and the pool still works.
    auto after = pool.submit([] { return 7; });
    REQUIRE(after.get() == 7);
}

TEST_CASE("ThreadPool queues what its workers cannot yet take", "[utils][pool]") {
    ThreadPool pool(1);

    // Hold the single worker on a task that will not return until this test says
    // so, which makes the queue depth below a fact rather than a race.
    std::promise<void> release;
    const std::shared_future<void> held = release.get_future().share();
    std::promise<void> started;
    std::future<void> has_started = started.get_future();

    auto blocked = pool.submit([&started, held] {
        started.set_value();
        held.wait();
    });
    has_started.wait();

    auto first = pool.submit([] { return 1; });
    auto second = pool.submit([] { return 2; });
    REQUIRE(pool.pending() == 2);

    release.set_value();
    blocked.get();
    REQUIRE(first.get() == 1);
    REQUIRE(second.get() == 2);
}

TEST_CASE("ThreadPool finishes queued work before it shuts down", "[utils][pool]") {
    // The promise ~ThreadPool makes, and the reason it drains rather than drops: a
    // discarded task destroys its promise unsatisfied, so the caller's get() throws
    // broken_promise from a destructor on another thread. Declared before the pool
    // so it outlives it.
    constexpr int kTasks = 64;
    std::atomic<int> completed{0};

    {
        ThreadPool pool(2);
        for (int i = 0; i < kTasks; ++i) {
            static_cast<void>(pool.submit([&completed] { completed.fetch_add(1); }));
        }
        // No wait_idle(): the destructor is what has to finish these.
    }

    REQUIRE(completed.load() == kTasks);
}

TEST_CASE("ThreadPool keeps a dropped future's task running", "[utils][pool]") {
    ThreadPool pool(1);
    std::atomic<int> completed{0};

    // Unlike std::async's future, this one does not block on destruction — the task
    // is queued and runs regardless of whether anyone kept the handle.
    static_cast<void>(pool.submit([&completed] { completed.fetch_add(1); }));
    pool.wait_idle();

    REQUIRE(completed.load() == 1);
}
