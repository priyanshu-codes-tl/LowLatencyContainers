# Low-Latency HFT Pipeline Project

A high-performance, ultra-low-latency engineering sandbox designed to simulate core components of a High-Frequency Trading (HFT) execution engine.

---

## 🏗️ Architecture Modules

As this project grows, core performance blocks are organized below:

### 1. Single-Producer Single-Consumer (SPSC) Ring Buffer
* **Status:** Complete 🏁
* **Core Optimization:** Lock-free, atomic index tracking using explicit memory ordering constraints (`acquire`/`release`) to bypass heavy OS mutex locks.
* **Math Hack:** Leverages bitwise `AND` masks (`&`) for instant $O(1)$ index wrapping, completely eliminating CPU-intensive division instructions.
* **Symmetric Array Batching:** Eradicates hardware cache ping-pong by reading and writing blocks of data simultaneously, driving data transport latency down to the bare metal limits.

### 2. Custom Fixed-Size Memory Pool
* **Status:** Complete 🏁
* **Core Optimization:** Bypassing `malloc` and the OS heap entirely to guarantee predictable $O(1)$ allocation times without runtime latency spikes.
* **Concurrency Engine:** Built using hardware-level Compare-And-Swap (`compare_exchange_weak`) retry loops to allow completely lock-free concurrent allocation and deallocation across multiple threads.
* **Memory Controls:** Fine-tuned utilizing `std::memory_order_release` barriers to enforce strict instruction sequencing—guaranteeing placement new data writes are completely visible in RAM before pointers change—alongside `std::memory_order_relaxed` for maximum velocity on loop retries.
* **Safety Primitives:** Hardened against high-contention edge cases using short-circuit null evaluation guards (`current_spot != nullptr`) to eliminate risk of null-pointer dereferencing (`Segmentation faults`) during rapid pool exhaustion.

### 3. Continuous Network Ingestion & Execution Engine
* **Status:** Complete 🏁
* **Network Simulation:** Mimics live market feeds by ingesting raw byte arrays and directly mapping them into memory-aligned C++ structs (`#pragma pack`), matching real-world UDP deserialization techniques.
* **Zero-Overhead Tracking:** Utilizes local batch indexing to track incoming ticks, completely eliminating modulo division from the hot path. 
* **Pipeline Resilience:** Engineered with robust backpressure spin-locks to guarantee zero packet loss during violent market data bursts. 
* **Graceful Termination:** Features a dynamic final-batch flush mechanism to ensure all in-flight strategy signals are safely vaulted into the memory pool prior to thread shutdown.

---

## ⚡ Performance & Benchmarks

Because low-latency code behaves uniquely depending on underlying silicon layout, performance characteristics are tracked against the host machine:

* **CPU Environment:** Hybrid Architecture (6 Performance Cores, 4 Efficiency Cores).
* **Cache Line Isolation:** `alignas(64)` implemented via custom node unions and global atomic flags to strictly prevent false sharing between neighboring core execution lines.
* **Thread Affinity:** Critical producer and consumer workloads are explicitly pinned (`pthread_setaffinity_np`) to isolated cores to bypass OS scheduler interference and context-switching overhead.
* **Core-to-Core Transport Latency:** Benchmarked at **27 nanoseconds per tick** during a 10-million operation symmetric batching simulation.

---

## 🎯 Strategic Targets & Roadmap

This execution engine serves as the foundational data transport layer. Upcoming development will focus on scaling the pipeline to handle live-market environments and specialized asset classes:

* **Index Tracking:** Building a localized Limit Order Book (LOB) to ingest and track intraday price movements for the Nifty 50 index.
* **Equity Volatility Processing:** Expanding the `marketUpdate` ingestion logic to process high-volume, deep-order-book data for banking sector equities (PNB, IDFC First, and Bank of Baroda).
* **Commodity Futures Adaptation:** Modifying the struct alignment and memory pool parameters to handle the distinct tick formats and margin structures of global commodity markets, specifically crude oil and gold futures.
* **Kernel Bypass (Future Scope):** Researching integration with hardware-accelerated networking frameworks (DPDK / OpenOnload) to eliminate PCIe DMA bottlenecks and ingest UDP streams directly into C++ user-space.
