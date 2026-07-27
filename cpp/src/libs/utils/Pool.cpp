// Implementation unit for utils:pool — everything about the pool that is not
// a template.
//
// This is a module implementation unit (`module utils;`, no `export`), so its
// global module fragment never becomes part of any BMI. That is what lets it
// include <thread>, <mutex>, <condition_variable> and <deque> freely while
// Pool.cppm, an interface unit, includes none of them: the pimpl'd State below is
// only ever named across the boundary, never defined there. Same rule that keeps
// <curl/curl.h> in Http.cpp and toml++ in Toml.cpp; see the note at the top of
// cup.project's io.cppm for the ICE that established it.
module;
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>
module utils;

namespace utils {

// State is the pool: the queue, the locks that guard it, and the workers.
//
// Its address is stable for the pool's lifetime (it is behind a unique_ptr that is
// never reseated and a ThreadPool that cannot move), which is what makes it safe
// for every worker to hold a bare reference to it.
struct ThreadPool::State {
    // One mutex for all of it. The queue, the in-flight count and the two
    // predicates below are a single invariant — "there is nothing left to do" is a
    // statement about the queue *and* the count together — and splitting the lock
    // would mean the two could disagree at the moment wait_idle() looked.
    mutable std::mutex mutex;

    // condition_variable_any rather than condition_variable: only the _any form
    // takes a std::stop_token overload of wait(), which is what lets a stop request
    // wake a worker that is parked. Without it, shutdown needs a sentinel task per
    // worker or a separate flag plus notify_all, both of which are the same idea
    // written worse.
    std::condition_variable_any ready;

    // Signalled when the last in-flight task finishes with an empty queue. Kept
    // separate from `ready` so waking a wait_idle() waiter does not wake every idle
    // worker with it.
    std::condition_variable_any idle;

    // Nested inside ThreadPool, which is what lets it name the private Task.
    std::deque<std::unique_ptr<Task>> queue;

    // Tasks taken off the queue but not yet finished. `queue.empty()` alone is not
    // idleness — it is true the instant a worker pops the last task and starts a
    // ten-second run.
    std::size_t active = 0;

    // The last data member, so it is destroyed *first*. Every jthread's destructor requests
    // a stop and joins, so by the time the queue and the mutex above are destroyed,
    // no thread can still be touching them. Reordering these members reintroduces
    // exactly the shutdown race the jthreads are here to remove.
    std::vector<std::jthread> workers;

    // run is one worker thread's whole life: take a task, run it, account for it,
    // repeat until stopped and drained.
    //
    // A member of State rather than a free function in an anonymous namespace,
    // because State is a *private* nested type: an out-of-class definition of one
    // is allowed, but a non-member naming `ThreadPool::State&` in its signature is
    // not. Making the type public to accommodate a helper would be the tail wagging
    // the dog.
    //
    // Declared here and defined below the struct purely so State reads as what it
    // is — the pool's data — rather than as a data member list with a thirty-line
    // loop in the middle of it. Either spelling compiles.
    void run(const std::stop_token& stop);

    // worker_main is what each std::jthread is actually constructed from, and its
    // existence is a GCC 14 workaround rather than a design.
    //
    // Constructing a std::thread or std::jthread *from a lambda* inside a module
    // translation unit ICEs the compiler:
    //
    //     during IPA pass: comdats
    //     internal compiler error: in ipa_comdats, at ipa-comdats.cc:355
    //
    // reported against the closing brace of the file and naming nothing. It is the
    // thread invoker's instantiated vtable — std::thread wraps the callable in an
    // internal _State_impl with a virtual _M_run() — landing in a comdat group the
    // pass cannot place for a module-attached TU. Constructing from a plain
    // *function pointer* takes a different instantiation and compiles, at -O0 and
    // at -O2 alike. (std::async from a lambda is also fine, which is why Phase 3's
    // release fetch never hit this.)
    //
    // So the callable is this static member and the pool's state travels as an
    // argument. A static member function is an ordinary function pointer, and being
    // a member is what lets it name the private State at all. std::jthread passes
    // the stop token as the first argument to anything invocable with one, so the
    // signature is what makes the token arrive.
    //
    // This is the same shape the port already settled on for its substitutable
    // seams — HttpGet is a bare function pointer, not a std::function — for a
    // different GCC 14 bug. See docs/migration-cpp23.md, rule 7.
    static void worker_main(std::stop_token stop, State* state);
};

void ThreadPool::State::worker_main(std::stop_token stop, State* state) { state->run(stop); }

void ThreadPool::State::run(const std::stop_token& stop) {
    for (;;) {
        std::unique_ptr<Task> task;
        {
            std::unique_lock lock(mutex);

            // Returns the predicate's value, and false if the stop was requested
            // while it was false. So: a stop with work still queued returns true
            // and the work runs — the draining behaviour ~ThreadPool promises, and
            // the reason a submitted task's future is never left broken.
            if (!ready.wait(lock, stop, [this] { return !queue.empty(); })) {
                return;
            }

            task = std::move(queue.front());
            queue.pop_front();
            ++active;
        }

        // Run unlocked — the whole point — and note that nothing here can throw:
        // every task reaching this queue was wrapped in a std::packaged_task by
        // ThreadPool::submit, which stores what the callable threw in the future's
        // shared state rather than letting it out. If some future caller enqueues
        // an unwrapped callable, that guarantee moves with it.
        task->run();
        // Destroyed before the accounting below, so a task's own destructor never
        // runs while this thread counts itself idle.
        task.reset();

        {
            const std::lock_guard guard(mutex);
            --active;
            if (active == 0 && queue.empty()) {
                idle.notify_all();
            }
        }
    }
}

namespace {

// worker_count resolves the constructor's argument. hardware_concurrency() is
// allowed to return 0 when it cannot tell (it does, in some containers), and a
// pool of zero threads accepts work that never runs — a hang rather than an
// error — so the floor is 1.
std::size_t worker_count(std::size_t requested) {
    if (requested > 0) {
        return requested;
    }
    return std::max<std::size_t>(1, std::thread::hardware_concurrency());
}

}  // namespace

ThreadPool::ThreadPool(std::size_t workers) : state_(std::make_unique<State>()) {
    const std::size_t count = worker_count(workers);
    state_->workers.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        // A function pointer plus an argument, not a lambda — see worker_main.
        //
        // The raw pointer is passed rather than `this` so a worker never reads
        // state_ — the unique_ptr member — while the constructor still filling it
        // runs; the State it points at is fully built by now. Safe for the pool's
        // whole life because State's address never changes and the vector holding
        // the workers is State's own last member, so their destructors run before
        // the rest of it.
        state_->workers.emplace_back(&State::worker_main, state_.get());
    }
}

// Declared in Pool.cppm and defined here rather than defaulted there, because
// State is incomplete in the interface unit — see the note on the member.
//
// The body is empty and the work is all in the member destructors: ~vector destroys
// each jthread, and ~jthread requests a stop and joins. Every worker then finishes
// its current task, drains the queue, and returns. There is no explicit
// request_stop() before the loop, and there does not need to be — but note the
// consequence: the joins are sequential, so shutdown costs as long as the longest
// remaining task, not the sum.
ThreadPool::~ThreadPool() = default;

void ThreadPool::enqueue(std::unique_ptr<Task> task) {
    {
        const std::lock_guard guard(state_->mutex);
        state_->queue.push_back(std::move(task));
    }
    // Notified outside the lock: a worker woken while the queue mutex is still held
    // would immediately block on it again.
    state_->ready.notify_one();
}

std::size_t ThreadPool::size() const noexcept { return state_->workers.size(); }

std::size_t ThreadPool::pending() const {
    const std::lock_guard guard(state_->mutex);
    return state_->queue.size();
}

void ThreadPool::wait_idle() {
    std::unique_lock lock(state_->mutex);
    state_->idle.wait(lock, [this] { return state_->queue.empty() && state_->active == 0; });
}

}  // namespace utils
