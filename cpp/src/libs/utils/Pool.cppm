module;
// <future> is the heavy one, and it is here because submit() returns a
// std::future and templates cannot be defined anywhere else. Everything else the
// pool needs to *work* — <thread>, <mutex>, <condition_variable>, <deque> — is in
// Pool.cpp, behind the State pimpl below, and so reaches no BMI at all. That is
// the same move cup.platform makes with libcurl and cup.project makes with
// toml++, applied to the standard library: an interface unit's global module
// fragment is the expensive place to put a header, and the pimpl is what makes
// leaving it out possible rather than merely tidy.
#include <concepts>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>
export module utils:pool;

export namespace utils {

// ThreadPool runs queued work on a fixed set of threads and hands each caller a
// std::future for its result:
//
//     ThreadPool pool;
//     auto gcc   = pool.submit(newest_gcc_releases);
//     auto clang = pool.submit(newest_clang_releases);
//     const auto compilers = gcc.get();      // blocks; rethrows what escaped
//
// What it replaces: cup.scaffold:releases spawns one std::async per fetch and
// joins them by destroying the futures. That is correct and, at two fetches, it is
// also cheaper than this — a pool is not an optimisation at n=2. The reason to
// have one anyway is that std::async's contract does not compose: with
// launch::async it creates a thread *per call*, so the moment two independent call
// sites each "just fetch a couple of things" the process decides how many threads
// to run by addition. A pool is the object that owns that decision. Phase 4's
// `cup register` and `cup docker` are where this starts to matter, since both walk
// a list of dependencies or tags.
//
// Not a singleton. A pool has a size, and the right size depends on what it is
// for — cup would want a small one for network fetches and a hardware-sized one
// for anything CPU-bound. Where a process wants one shared pool, bind it as a
// service (:service) rather than deriving it from Singleton, which keeps the
// number of threads a decision main() makes rather than one the type makes.
class ThreadPool {
public:
    // Constructs a pool with `workers` threads, or hardware_concurrency() when
    // that is 0 — and 1 if the implementation will not say, which it is allowed
    // not to.
    explicit ThreadPool(std::size_t workers = 0);

    // Stops accepting work, lets everything already queued finish, and joins.
    //
    // Draining rather than dropping is the deliberate part, and futures are why: a
    // task discarded before it runs destroys its promise unsatisfied, so the
    // caller's future.get() throws std::future_error(broken_promise) — an
    // exception raised by a *destructor somewhere else*, which is about the worst
    // diagnostic in the language. Anything already submitted therefore runs. A
    // caller who wants abandonment wants a stop token in the task itself.
    ~ThreadPool();

    // A pool owns threads that hold a pointer to its state, so it neither copies
    // nor moves. Hold it by reference, or in a shared_ptr through :service.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // submit queues fn(args...) and returns a future for its result.
    //
    // The arguments are copied (or moved) into the task, exactly as std::async and
    // std::thread do, and for the same reason: the call happens later, on another
    // thread, and a reference to the caller's frame may be dangling by then. Pass
    // std::ref deliberately where a reference is what is wanted.
    //
    // Exceptions do not escape a worker. std::packaged_task catches whatever the
    // task throws and stores it in the shared state, so it surfaces from the
    // caller's .get() — which is the one place with the context to handle it. cup
    // itself returns std::expected rather than throwing, so in cup's own code this
    // is a backstop rather than a channel.
    //
    // The result is [[nodiscard]] because dropping the future is a decision, not
    // an oversight: the task still runs, but nothing can observe its result or its
    // exception. Say `static_cast<void>(pool.submit(...))` where that is meant.
    // (Note that this future does *not* block on destruction. Only std::async's
    // does, and that behaviour is widely considered its design mistake.)
    template <typename Fn, typename... Args>
        requires std::invocable<Fn, Args...>
    [[nodiscard]] std::future<std::invoke_result_t<Fn, Args...>> submit(Fn&& fn,
                                                                       Args&&... args) {
        using Result = std::invoke_result_t<Fn, Args...>;

        std::packaged_task<Result()> task(
            [fn = std::forward<Fn>(fn), ... captured = std::forward<Args>(args)]() mutable
            -> Result { return std::invoke(std::move(fn), std::move(captured)...); });
        std::future<Result> result = task.get_future();

        // Wrapped in cup's own Task rather than in a std::move_only_function — see
        // the note on Task below for why the obvious C++23 spelling is unavailable
        // on GCC 14.
        enqueue(std::make_unique<HeldTask<std::packaged_task<Result()>>>(std::move(task)));
        return result;
    }

    // size is the number of worker threads, fixed at construction.
    [[nodiscard]] std::size_t size() const noexcept;

    // pending is the number of tasks queued but not yet started. A snapshot: by
    // the time it returns, a worker may have taken one. Useful for a test or a
    // progress line, not for control flow.
    [[nodiscard]] std::size_t pending() const;

    // wait_idle blocks until the queue is empty and no task is running.
    //
    // For a caller that submitted work it does not need results from and wants to
    // know is done — the fan-out cup.scaffold's release fetch would use if it did
    // not need the values. It must not be called *from* a task: the pool would be
    // waiting for work that cannot finish until the wait does.
    void wait_idle();

private:
    // Task is one unit of queued work with its callable's type erased away: the
    // queue holds these, and a worker only ever calls run().
    //
    // The C++23 spelling of this is std::move_only_function<void()>, and that is
    // what the queue held first. It does not survive a module boundary on GCC 14 —
    // a consumer instantiating submit() gets
    //
    //     error: cannot convert '...submit<...>(...)::<lambda()>'
    //            to 'std::move_only_function@utils<void()>'
    //
    // for every call, because the wrapper's constructor constraints come back
    // module-attached and stop matching. Same family as the std::unordered_map
    // failure in Service.cppm: a standard-library class template instantiated
    // *across* the boundary rather than inside one side of it. Here the erasure is
    // three lines to write by hand, so it is written by hand — and it is the one
    // std::function cannot do anyway, since std::packaged_task is move-only and
    // every pre-C++23 codebase works around that with a shared_ptr and an extra
    // allocation. See docs/migration-cpp23.md, rule 6.
    //
    // Private and nested: nothing outside the pool constructs one. State, being a
    // nested class of ThreadPool, can name it in Pool.cpp.
    struct Task {
        Task() = default;
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        Task(Task&&) = delete;
        Task& operator=(Task&&) = delete;
        virtual ~Task() = default;
        virtual void run() = 0;
    };

    // HeldTask is the one implementation: it owns the callable and calls it. The
    // template parameter is always a std::packaged_task, which is what makes run()
    // non-throwing from the worker's point of view.
    template <typename Callable>
    class HeldTask final : public Task {
    public:
        explicit HeldTask(Callable callable) : callable_(std::move(callable)) {}
        void run() override { callable_(); }

    private:
        Callable callable_;
    };

    // enqueue is the type-erased half of submit — the part that does not depend on
    // Fn, so it can be a plain function, declared here and defined in Pool.cpp.
    // That split is what keeps the mutex and the queue out of this file: the
    // template above cannot touch them, and does not need to.
    //
    // It is also the house rule from Phase 3 (partitions declare, implementation
    // units define) applied where it is free.
    void enqueue(std::unique_ptr<Task> task);

    // State holds the threads, the queue and the locks — every member whose type
    // would drag <thread> and <mutex> into this interface unit. Defined in
    // Pool.cpp; incomplete here, which is why the destructor is declared and not
    // defaulted (unique_ptr's deleter needs the complete type, and `= default`
    // here would instantiate it against the incomplete one).
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace utils
