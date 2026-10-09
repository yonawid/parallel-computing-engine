#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>
#include <string>

#include "parallel_algorithms.hpp"
#include "thread_pool.hpp"

std::string escape_json(const std::string& s) {
    return "\"" + s + "\"";
}

struct BenchmarkResult {
    std::string name;
    std::size_t size;
    std::size_t workers;
    double seq_time_ms;
    double par_time_ms;
    double speedup;
    double efficiency;
};

std::vector<BenchmarkResult> results;

template <typename Func>
double measure_time_ms(Func&& func) {
    auto start = std::chrono::steady_clock::now();
    func();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void benchmark_reduction() {
    std::vector<std::size_t> sizes = {1000000, 10000000, 50000000};
    std::vector<std::size_t> worker_counts = {1, 2, 4, 6};

    for (auto size : sizes) {
        std::vector<long long> v(size, 1LL);

        // Sequential
        double seq_time = 0;
        long long seq_result = 0;
        for (int iter = 0; iter < 5; ++iter) {
            double t = measure_time_ms([&] {
                seq_result = std::accumulate(v.begin(), v.end(), 0LL);
            });
            if (iter == 0 || t < seq_time) seq_time = t;
        }

        for (auto workers : worker_counts) {
            pce::ThreadPool pool(workers);
            
            double par_time = 0;
            long long par_result = 0;
            for (int iter = 0; iter < 5; ++iter) {
                double t = measure_time_ms([&] {
                    par_result = pce::parallel_reduce(pool, v.begin(), v.end(), 0LL, std::plus<long long>());
                });
                if (iter == 0 || t < par_time) par_time = t;
            }

            if (seq_result != par_result) {
                std::cerr << "Mismatch in reduction!\n";
            }

            double speedup = seq_time / par_time;
            double efficiency = speedup / workers;

            results.push_back({"Reduction", size, workers, seq_time, par_time, speedup, efficiency});
        }
    }
}

void benchmark_matmul() {
    std::vector<std::size_t> sizes = {200, 500, 800}; // Using moderate sizes for limited RAM
    std::vector<std::size_t> worker_counts = {1, 2, 4, 6};
    
    std::mt19937 gen(42);
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    for (auto N : sizes) {
        std::vector<float> A(N * N);
        std::vector<float> B(N * N);
        std::vector<float> C_seq(N * N);
        std::vector<float> C_par(N * N);

        for (auto& x : A) x = dis(gen);
        for (auto& x : B) x = dis(gen);

        double seq_time = 0;
        for (int iter = 0; iter < 3; ++iter) {
            double t = measure_time_ms([&] {
                pce::sequential_matrix_multiply(A, B, C_seq, N, N, N);
            });
            if (iter == 0 || t < seq_time) seq_time = t;
        }

        for (auto workers : worker_counts) {
            pce::ThreadPool pool(workers);
            
            double par_time = 0;
            for (int iter = 0; iter < 3; ++iter) {
                double t = measure_time_ms([&] {
                    pce::parallel_matrix_multiply(pool, A, B, C_par, N, N, N);
                });
                if (iter == 0 || t < par_time) par_time = t;
            }

            double speedup = seq_time / par_time;
            double efficiency = speedup / workers;

            results.push_back({"MatMul", N, workers, seq_time, par_time, speedup, efficiency});
        }
    }
}

int main() {
    std::cout << "Running Benchmarks (Thread pool creation is EXCLUDED from timings)...\n";
    benchmark_reduction();
    benchmark_matmul();

    // Console output
    std::cout << "\n=== Benchmark Summary ===\n";
    std::cout << std::left << std::setw(15) << "Operation" 
              << std::setw(12) << "Size" 
              << std::setw(10) << "Workers" 
              << std::setw(15) << "Seq Time(ms)" 
              << std::setw(15) << "Par Time(ms)" 
              << std::setw(12) << "Speedup" 
              << std::setw(12) << "Efficiency" << "\n";
    
    for (const auto& r : results) {
        std::cout << std::left << std::setw(15) << r.name 
                  << std::setw(12) << r.size 
                  << std::setw(10) << r.workers 
                  << std::setw(15) << std::fixed << std::setprecision(2) << r.seq_time_ms 
                  << std::setw(15) << r.par_time_ms 
                  << std::setw(12) << r.speedup 
                  << std::setw(12) << r.efficiency << "\n";
    }

    // Write CSV
    std::ofstream csv("benchmarks.csv");
    csv << "Operation,Size,Workers,SeqTimeMs,ParTimeMs,Speedup,Efficiency\n";
    for (const auto& r : results) {
        csv << r.name << "," << r.size << "," << r.workers << "," 
            << r.seq_time_ms << "," << r.par_time_ms << "," 
            << r.speedup << "," << r.efficiency << "\n";
    }

    // Write JSON
    std::ofstream json("benchmarks.json");
    json << "[\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        json << "  {\n"
             << "    \"operation\": \"" << r.name << "\",\n"
             << "    \"size\": " << r.size << ",\n"
             << "    \"workers\": " << r.workers << ",\n"
             << "    \"seq_time_ms\": " << r.seq_time_ms << ",\n"
             << "    \"par_time_ms\": " << r.par_time_ms << ",\n"
             << "    \"speedup\": " << r.speedup << ",\n"
             << "    \"efficiency\": " << r.efficiency << "\n"
             << "  }";
        if (i < results.size() - 1) json << ",";
        json << "\n";
    }
    json << "]\n";

    return 0;
}
