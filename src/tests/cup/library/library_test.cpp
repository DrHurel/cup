#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

import cup.library;
import cup.cmd;
import cup.error;
import cup.log;
import cup.project;
import cup.scaffold;
import cup.tmpl;
import cup.ui;
import core;

namespace {

namespace library = cup::library;

// What the fake libraries below did, in order.
std::vector<std::string>& events() {
    static std::vector<std::string> log;
    return log;
}

// A diamond: Top depends on Left and Right, which both depend on Base.
struct Base {
    using dependencies = std::tuple<>;
    static void initialize() { events().emplace_back("+base"); }
    static void finalize() { events().emplace_back("-base"); }
};
struct Left {
    using dependencies = std::tuple<Base>;
    static void initialize() { events().emplace_back("+left"); }
    static void finalize() { events().emplace_back("-left"); }
};
struct Right {
    using dependencies = std::tuple<Base>;
    static void initialize() { events().emplace_back("+right"); }
    static void finalize() { events().emplace_back("-right"); }
};
struct Top {
    using dependencies = std::tuple<Left, Right>;
    static void initialize() { events().emplace_back("+top"); }
    static void finalize() { events().emplace_back("-top"); }
};

// Fails to initialize after its dependency came up.
struct Broken {
    using dependencies = std::tuple<Base>;
    static void initialize() { throw std::runtime_error("broken"); }
    static void finalize() { events().emplace_back("-broken"); }
};

struct NoHooks {
    using dependencies = std::tuple<>;
};
struct NotATuple {
    using dependencies = Base;
    static void initialize() {}
    static void finalize() {}
};

using Events = std::vector<std::string>;

}  // namespace

TEST_CASE("Declaration requires a dependencies tuple and both hooks", "[library]") {
    STATIC_REQUIRE(library::Declaration<Top>);
    STATIC_REQUIRE_FALSE(library::Declaration<NoHooks>);
    STATIC_REQUIRE_FALSE(library::Declaration<NotATuple>);
}

TEST_CASE("depends_on follows dependencies transitively", "[library]") {
    STATIC_REQUIRE(library::depends_on_v<Top, Left>);
    STATIC_REQUIRE(library::depends_on_v<Top, Base>);
    STATIC_REQUIRE_FALSE(library::depends_on_v<Left, Right>);
    STATIC_REQUIRE_FALSE(library::depends_on_v<Base, Top>);
    STATIC_REQUIRE_FALSE(library::depends_on_v<Top, Top>);
}

TEST_CASE("initialize brings dependencies up first, each once; finalize reverses it",
          "[library]") {
    events().clear();
    library::initialize<Top>();
    CHECK(events() == Events{"+base", "+left", "+right", "+top"});
    CHECK(library::initialized_count() == 4);

    library::initialize<Top>();
    library::initialize<Left>();
    CHECK(events().size() == 4);

    events().clear();
    library::finalize();
    CHECK(events() == Events{"-top", "-right", "-left", "-base"});
    CHECK(library::initialized_count() == 0);

    // Finalized libraries come back up on the next initialize.
    events().clear();
    library::initialize<Left>();
    CHECK(events() == Events{"+base", "+left"});
    library::finalize();
}

TEST_CASE("a Scope finalizes only what it initialized", "[library]") {
    events().clear();
    {
        const library::Scope<Left> outer;
        {
            const library::Scope<Top> inner;
            CHECK(events() == Events{"+base", "+left", "+right", "+top"});
        }
        CHECK(events() == Events{"+base", "+left", "+right", "+top", "-top", "-right"});
        CHECK(library::initialized_count() == 2);
    }
    CHECK(events().back() == "-base");
    CHECK(library::initialized_count() == 0);
}

TEST_CASE("a Scope whose initialize throws finalizes what came up before it", "[library]") {
    events().clear();
    CHECK_THROWS_AS(library::Scope<Broken>{}, std::runtime_error);
    CHECK(events() == Events{"+base", "-base"});
    CHECK(library::initialized_count() == 0);
}

TEST_CASE("every library of cup declares its dependencies", "[library]") {
    STATIC_REQUIRE(library::Declaration<cup::library::Library>);
    STATIC_REQUIRE(library::Declaration<cup::error::Library>);
    STATIC_REQUIRE(library::Declaration<core::Library>);
    STATIC_REQUIRE(library::Declaration<cup::ui::Library>);
    STATIC_REQUIRE(library::Declaration<cup::tmpl::Library>);
    STATIC_REQUIRE(library::Declaration<cup::project::Library>);
    STATIC_REQUIRE(library::Declaration<cup::scaffold::Library>);
    STATIC_REQUIRE(library::Declaration<cup::log::Library>);
    STATIC_REQUIRE(library::Declaration<cup::cmd::Library>);

    STATIC_REQUIRE(std::tuple_size_v<cup::error::Library::dependencies> == 0);
    STATIC_REQUIRE(library::depends_on_v<core::Library, cup::error::Library>);
    STATIC_REQUIRE(library::depends_on_v<cup::scaffold::Library, core::Library>);
    STATIC_REQUIRE_FALSE(library::depends_on_v<cup::ui::Library, cup::tmpl::Library>);
    STATIC_REQUIRE_FALSE(library::depends_on_v<core::Library, cup::ui::Library>);
}

TEST_CASE("cup.cmd's Scope brings up the whole graph, once each", "[library]") {
    {
        const library::Scope<cup::cmd::Library> libraries;
        // error, library, core, log, project, tmpl, ui, scaffold and cmd.
        CHECK(library::initialized_count() == 9);
    }
    CHECK(library::initialized_count() == 0);
}
