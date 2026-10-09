# C++20 Parallel Computing Engine

A from-scratch C++20 parallel computing engine built with the standard library only (no Boost, OpenMP, TBB). This repository implements a fully featured engine, spanning from low-level thread pool execution to higher-level parallel algorithms and benchmarks. CPU only.

## Features Completed
- **Phase 1: Thread Pool & Safe Queue**
  - Configurable fixed-size worker pool, worker reuse.
  - `submit()` for lambdas, functions, functors; void and non-void results.
  - Exception propagation, move-only callables/arguments support.
  - Graceful, idempotent, thread-safe shutdown; destructor drains accepted tasks.
  - Concurrent producers using a blocking, closable FIFO queue.
- **Phase 2: Priority & Statistics**
  - Priority-aware task scheduling (`TaskPriority::High`, `Normal`, `Low`).
  - Execution statistics (`tasks_submitted`, `tasks_completed`, `tasks_failed`).
  - Active worker tracking (`current_active_workers`, `peak_active_workers`).
- **Phase 3: Parallel Algorithms**
  - `pce::parallel_reduce` for concurrent reduction operations on iterators.
  - `pce::parallel_matrix_multiply` for tiled and parallel dense matrix multiplication.
- **Phase 4: Benchmarking**
  - Custom benchmarking suite to evaluate sequential vs parallel speedups.
  - CSV and JSON output format generation for analysis.
  - Actual measured performance on an MSVC 2026 test system:

| Operation | Size | Workers | Seq Time(ms) | Par Time(ms) | Speedup | Efficiency |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Reduction | 1,000,000 | 2 | 0.43 | 0.33 | 1.31 | 0.66 |
| Reduction | 10,000,000 | 4 | 6.23 | 3.15 | 1.98 | 0.49 |
| Reduction | 50,000,000 | 6 | 29.29 | 15.35 | 1.91 | 0.32 |
| MatMul | 200 | 2 | 5.78 | 12.01 | 0.48 | 0.24 |
| MatMul | 500 | 4 | 99.45 | 109.90 | 0.90 | 0.23 |
| MatMul | 800 | 6 | 437.73 | 238.66 | 1.83 | 0.31 |
- **Phase 5: Release & CI**
  - Automated CI validation with GitHub Actions across Ubuntu, Windows, and macOS.
  - Stress testing capabilities with comprehensive unit tests for thread pool and algorithms.

## Architecture

```
 producers ──submit()──► ThreadPool ──push()──► TaskQueue ◄──wait_and_pop()── workers (std::thread × N)
                │                                (mutex + condvar)
                └──► std::future<T> ◄── std::packaged_task<T()> (runs on a worker)
```

| File | Role |
|------|------|
| `include/task_queue.hpp` | `pce::TaskQueue`: blocking, closable priority queue of `std::function<void()>` |
| `include/thread_pool.hpp` | `pce::ThreadPool`: templated `submit()`, lifecycle management |
| `include/execution_stats.hpp` | Thread-safe recorder and struct for statistics |
| `include/task_priority.hpp` | Definition of execution priorities |
| `include/parallel_algorithms.hpp`| `parallel_reduce` and `parallel_matrix_multiply` |
| `src/thread_pool.cpp` | construction, worker loop, shutdown |
| `examples/main.cpp` | Demo of thread pool and exception handling |
| `tests/test_thread_pool.cpp` | Custom test harness for threading edge cases |
| `tests/test_algorithms.cpp` | Validation of parallel algorithms correctness |
| `benchmarks/benchmark.cpp` | Evaluation framework generating speedup metrics |

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (MSVC 2019+, GCC 11+, Clang 14+).

### Windows (MSVC)
```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\pce_example.exe
.\build\Release\pce_benchmark.exe
```
Use `--config Debug` for Debug builds. Run from a "Developer PowerShell for VS" or let CMake locate Visual Studio.

### Linux & macOS
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/pce_example
./build/pce_benchmark
```
ThreadSanitizer (GCC/Clang): `cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPCE_ENABLE_TSAN=ON`.

## Limitations
- Unbounded queue (no back-pressure).
- Strict priority scheduling can starve low-priority tasks under continuous high-priority load.
- A task that blocks on the result of another task queued on the same pool can deadlock when workers are exhausted (such as running `parallel_matrix_multiply` recursively inside a thread pool thread).
- Tests passing does not prove absence of races; run under ThreadSanitizer on Linux/Clang/GCC where available.
