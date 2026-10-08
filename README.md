# parallel-computing-engine

A from-scratch C++20 parallel computing engine built with the standard library only (no Boost, OpenMP, TBB). This repository currently contains **Phase 1**: a reusable thread pool and a thread-safe task queue with deterministic correctness tests. CPU only.

## Phase 1 architecture

```
 producers ──submit()──► ThreadPool ──push()──► TaskQueue ◄──wait_and_pop()── workers (std::thread × N)
                │                                (mutex + condvar)
                └──► std::future<T> ◄── std::packaged_task<T()> (runs on a worker)
```

| File | Role |
|------|------|
| `include/task_queue.hpp` | `pce::TaskQueue`: blocking, closable FIFO of `std::function<void()>` |
| `include/thread_pool.hpp` | `pce::ThreadPool`: templated `submit()`, lifecycle docs |
| `src/thread_pool.cpp` | construction, worker loop, shutdown |
| `examples/main.cpp` | demo: 10 compute tasks, a void task, a throwing task |
| `tests/test_thread_pool.cpp` | custom test harness, 20 test cases |

### Task queue
- `push()` appends under a mutex and notifies one waiter; returns `false` once closed.
- `wait_and_pop()` blocks on a condition variable with a predicate (spurious-wakeup safe, no busy waiting). It returns `false` only when the queue is closed **and** empty, so queued work is drained after `close()`.
- `close()` is idempotent and wakes all waiters. Tasks are never executed under the queue mutex. Internal state is not exposed.

### Thread pool
- Workers loop on `wait_and_pop()` and run each task with no lock held.
- `submit()` decay-copies/moves the callable and arguments into a `std::packaged_task` (held by `shared_ptr` because `std::function` requires copyable targets), so move-only callables/arguments work and no references dangle. Exceptions surface through the future.
- Shutdown = close the queue, workers drain it, join. It is guarded by `std::call_once`, so it is idempotent and concurrent callers all return only after the join completes. The queue's closed flag is the single source of truth for rejecting submissions (throws `std::runtime_error`).
- If spawning a worker throws, already-started workers are closed and joined before rethrowing, so no joinable thread is destroyed.
- Calling `shutdown()` from a task running on the same pool throws `std::logic_error` (a worker cannot join itself).
- `ThreadPool(0)` throws `std::invalid_argument`.

### Concurrency primitives
`std::thread`, `std::mutex`, `std::condition_variable`, `std::once_flag`, `std::packaged_task`/`std::future`, `std::atomic` (tests only).

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (MSVC 2019+, GCC 11+, Clang 14+).

### Windows (MSVC)
```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\pce_example.exe
```
Use `--config Debug` for Debug builds. Run from a "Developer PowerShell for VS" or let CMake locate Visual Studio.

### Linux
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/pce_example
```
ThreadSanitizer (GCC/Clang): `cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPCE_ENABLE_TSAN=ON`.

## Supported features
- Configurable fixed-size worker pool, worker reuse
- `submit()` for lambdas, functions, functors; void and non-void results
- Exception propagation, move-only callables/arguments
- Graceful, idempotent, thread-safe shutdown; destructor drains accepted tasks
- Concurrent producers

## Current limitations
- Single shared FIFO queue (one mutex; no work stealing, priorities, or cancellation)
- Unbounded queue (no back-pressure)
- Fixed thread count; no statistics
- A task that blocks on the result of another task queued on the same pool can deadlock when workers are exhausted
- Tests passing does not prove absence of races; run under ThreadSanitizer on Linux/Clang/GCC where available

## Roadmap (not yet implemented)
- **Phase 2:** Advanced task scheduling and execution statistics
- **Phase 3:** Parallel algorithms and matrix multiplication
- **Phase 4:** Performance benchmarking and scalability analysis
- **Phase 5:** Stress testing, documentation, and GitHub release
