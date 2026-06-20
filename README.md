# Simple Ring Buffer (Single-Threaded Queue)

A high-performance, single-threaded circular queue implemented in modern C++. This project serves as the architectural foundation for building multi-threaded, lock-free communication pipelines used in ultra-low latency systems and high-frequency trading (HFT) engines.

## 🛠️ Design Architecture

Unlike a standard `std::queue` which dynamically allocates memory on the Heap during execution (introducing severe latency spikes), this implementation utilizes a fixed-size `std::array` pre-allocated entirely on the stack.

* **O(1) Bounds:** Elements are added and removed in constant time without any dynamic allocations.
* **Wrap-Around Logic:** Pointers slide across the contiguous array and automatically wrap back to index zero using modulo operations, maintaining a highly optimized loop layout.

## 🏎️ Performance & Benchmarking

The repository contains an inline assembly-benchmarked tracking loop inside `main.cpp` to measure raw execution speed while preventing the compiler from completely optimizing away the execution path.

### Benchmark Profile:
* **Iterations:** 10,000,000 Push/Pop pairs
* **Compiler Optimization Level:** -O3

### Results:
[Insert your exact average time per Push/Pop pair in nanoseconds here from your terminal output!]

## 🚀 Future Roadmap & Optimizations

This version is a functional, single-threaded prototype. To upgrade this component into a production-grade HFT hot-path queue, the next evolutionary iterations will implement:
1. **Lock-Free Concurrency:** Upgrading pointers to `std::atomic` variables using `std::memory_order_release` and `std::memory_order_acquire` to allow safe multi-threaded processing.
2. **Bitwise Wrapping:** Restricting the buffer size to a power of 2 ($2^n$) to replace the heavy CPU division modulo operator (`%`) with a single-cycle bitwise `AND` mask (`&`).
3. **Cache Line Isolation:** Applying `alignas(64)` to internal data and independent pointer structures to physically eradicate **False Sharing** and optimize the CPU's hardware L1/L2 cache efficiency.
