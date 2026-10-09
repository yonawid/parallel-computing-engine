#pragma once

#include <algorithm>
#include <future>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "thread_pool.hpp"

namespace pce {

// parallel_reduce
// Divides the range [first, last) into independent chunks and reduces them using the
// provided ThreadPool. The operation must be associative.
//
// Because the operation is not assumed to have an identity element, each chunk's reduction
// is initialized with the first element of that chunk (cast to type T). The final
// result is obtained by sequentially reducing the chunk results, starting with 'initial'.
// Arbitrary non-associative operations (including strict floating-point arithmetic)
// may produce different results compared to sequential reduction due to evaluation order.
template <typename Iterator, typename T, typename BinaryOp>
T parallel_reduce(ThreadPool& pool, Iterator first, Iterator last, T initial, BinaryOp operation) {
    const std::size_t length = static_cast<std::size_t>(std::distance(first, last));
    if (length == 0) {
        return initial;
    }

    const std::size_t pool_size = pool.size();
    const std::size_t target_chunks = pool_size * 4;
    std::size_t chunk_size = length / target_chunks;
    if (chunk_size < 1024) {
        chunk_size = 1024;
    }
    if (chunk_size > length) {
        chunk_size = length;
    }

    const std::size_t num_chunks = (length + chunk_size - 1) / chunk_size;

    std::vector<std::future<T>> futures;
    futures.reserve(num_chunks);

    Iterator chunk_start = first;
    for (std::size_t i = 0; i < num_chunks; ++i) {
        Iterator chunk_end = chunk_start;
        std::size_t advance = std::min(chunk_size, static_cast<std::size_t>(std::distance(chunk_start, last)));
        std::advance(chunk_end, advance);

        // Submit chunk to thread pool
        futures.push_back(pool.submit([chunk_start, chunk_end, operation]() -> T {
            Iterator it = chunk_start;
            T chunk_result = static_cast<T>(*it);
            ++it;
            for (; it != chunk_end; ++it) {
                chunk_result = operation(std::move(chunk_result), *it);
            }
            return chunk_result;
        }));

        chunk_start = chunk_end;
    }

    // Combine results
    T result = std::move(initial);
    for (auto& f : futures) {
        result = operation(std::move(result), f.get());
    }

    return result;
}

// sequential_matrix_multiply
template <typename T>
void sequential_matrix_multiply(
    const std::vector<T>& A,
    const std::vector<T>& B,
    std::vector<T>& C,
    std::size_t M, std::size_t K, std::size_t N) {

    if (A.size() != M * K || B.size() != K * N || C.size() != M * N) {
        throw std::invalid_argument("Matrix dimensions do not match vector sizes.");
    }

    if (M == 0 || K == 0 || N == 0) {
        for (std::size_t i = 0; i < C.size(); ++i) C[i] = T{};
        return;
    }

    // i-k-j loop for better cache locality (contiguous access on B and C)
    for (std::size_t i = 0; i < M; ++i) {
        for (std::size_t n = 0; n < N; ++n) {
            C[i * N + n] = T{};
        }
        for (std::size_t k = 0; k < K; ++k) {
            T a_ik = A[i * K + k];
            for (std::size_t n = 0; n < N; ++n) {
                C[i * N + n] += a_ik * B[k * N + n];
            }
        }
    }
}

// parallel_matrix_multiply
// Note: This function blocks the calling thread until all chunks finish.
// Submitting a parallel_matrix_multiply task from within a thread pool worker
// to the same pool can cause a deadlock if the pool is saturated.
template <typename T>
void parallel_matrix_multiply(
    ThreadPool& pool,
    const std::vector<T>& A,
    const std::vector<T>& B,
    std::vector<T>& C,
    std::size_t M, std::size_t K, std::size_t N) {

    if (A.size() != M * K || B.size() != K * N || C.size() != M * N) {
        throw std::invalid_argument("Matrix dimensions do not match vector sizes.");
    }
    
    if (M == 0 || K == 0 || N == 0) {
        for (std::size_t i = 0; i < C.size(); ++i) C[i] = T{};
        return;
    }

    const std::size_t pool_size = pool.size();
    const std::size_t target_chunks = pool_size * 4;
    std::size_t rows_per_chunk = M / target_chunks;
    if (rows_per_chunk < 1) rows_per_chunk = 1;
    
    const std::size_t num_chunks = (M + rows_per_chunk - 1) / rows_per_chunk;

    std::vector<std::future<void>> futures;
    futures.reserve(num_chunks);

    for (std::size_t chunk = 0; chunk < num_chunks; ++chunk) {
        std::size_t row_start = chunk * rows_per_chunk;
        std::size_t row_end = std::min(row_start + rows_per_chunk, M);

        futures.push_back(pool.submit([&A, &B, &C, row_start, row_end, K, N]() {
            for (std::size_t i = row_start; i < row_end; ++i) {
                for (std::size_t n = 0; n < N; ++n) {
                    C[i * N + n] = T{};
                }
                for (std::size_t k = 0; k < K; ++k) {
                    T a_ik = A[i * K + k];
                    for (std::size_t n = 0; n < N; ++n) {
                        C[i * N + n] += a_ik * B[k * N + n];
                    }
                }
            }
        }));
    }

    for (auto& f : futures) {
        f.get();
    }
}

} // namespace pce
