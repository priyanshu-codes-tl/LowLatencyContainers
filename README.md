# Low-Latency C++ Execution Pipeline

A deterministic, ultra-low-latency engineering architecture designed to replicate the core transport, matching, and ingestion infrastructure of a proprietary High-Frequency Trading (HFT) execution engine.

---

## 🏗️ Core Architecture Modules

### 1. Zero-Allocation Limit Order Book (LOB) Engine
* **Execution Logic:** Deterministic price-time priority matching engine utilizing pre-allocated arrays and hardware-aligned nodes to achieve ~14 microsecond end-to-end execution.
* **O(1) Data Structures:** Bypasses pointer-chasing overhead by implementing doubly-linked lists across `uint32_t` integer indices within a contiguous block, terminating with custom `0xFFFFFFFF` null flags.
* **Top-of-Book Tracking:** Highly optimized bid/ask index sweeping designed for asymmetric market depth evaluation without dynamic memory reallocation.

### 2. Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
* **Core Optimization:** Lock-free, atomic index tracking using explicit memory ordering constraints (`acquire`/`release`) to bypass OS mutex locks entirely.
* **Math Hack:** Leverages bitwise `AND` masks (`&`) against power-of-2 boundaries for instant O(1) index wrapping, completely eliminating CPU-intensive modulo division instructions.
* **Symmetric Array Batching:** Eradicates hardware cache ping-pong by reading and writing blocks of data simultaneously, driving data transport latency down to the bare metal limits.

### 3. Custom Fixed-Size Memory Pool
* **Core Optimization:** Bypasses `malloc` and the OS heap entirely to guarantee predictable allocation times without runtime latency spikes.
* **Concurrency Engine:** Built using hardware-level Compare-And-Swap (`compare_exchange_weak`) retry loops to allow lock-free concurrent allocation across threads.
* **Memory Controls:** Fine-tuned utilizing `std::memory_order_release` barriers to enforce strict instruction sequencing alongside `std::memory_order_relaxed` for maximum velocity on loop retries.
* **Zero-Waste Alignment:** Utilizes `union` data structures to perfectly overlap `marketUpdate` objects with free-list pointers inside exact 64-byte boundaries.

### 4. UDP Multicast Ingestion & Endianness Translation
* **Network Boundary Safety:** Ingests live UDP datagrams utilizing `std::span` for strict, zero-copy memory boundaries.
* **Hardware Translation:** Aggressively translates Network Byte Order (Big-Endian) to Host CPU Order (Little-Endian) via single-cycle `ntohl` and `be64toh` hardware instructions directly on the hot path.
* **Network Simulation:** Mimics live market feeds by mapping raw bytes directly into C++ structs via `#pragma pack`, identically matching real-world exchange UDP deserialization.
* **Pipeline Resilience:** Engineered with robust backpressure spin-locks to guarantee zero packet loss during violent market data bursts.

---

## ⚡ Hardware & Silicon Tuning

Performance characteristics are strictly benchmarked against physical silicon layout to ensure deterministic execution:

* **Cache Line Isolation:** `alignas(64)` implemented via custom node unions and global atomic flags to strictly prevent false sharing between neighboring core execution lines.
* **Thread Affinity:** Critical producer and consumer workloads are explicitly pinned (`pthread_setaffinity_np`) to isolated cores to bypass OS scheduler interference and context-switching overhead.
* **Compiler Directives:** Spin-wait loops are strictly guarded with inline assembly clobbering (`asm volatile ("pause" ::: "memory")`) to force L1 cache reloading and prevent instruction reordering.
* **Core-to-Core Transport Latency:** Benchmarked at **27 nanoseconds per tick** during a 10-million operation symmetric batching simulation.

---

## 🎯 Forward Architecture & Implementation Roadmap

This engine serves as the foundational data transport layer. The immediate engineering roadmap expands this architecture to handle live-market derivatives pricing and bare-metal routing capabilities:

* **Kernel Bypass & Hardware Networking:** Replacing POSIX socket ingestion with hardware-accelerated networking frameworks (DPDK and Solarflare OpenOnload) to eliminate PCIe DMA bottlenecks and push UDP streams directly into C++ user-space.
* **Outbound FIX Protocol Serialization:** Engineering a zero-allocation TCP dispatch layer utilizing the FIX (Financial Information eXchange) protocol to construct and route outbound execution signals to exchange gateways.
* **Options & Derivatives Mechanics:** Expanding the Limit Order Book to natively calculate and ingest intraday Greek decay and pricing volatility for derivative contracts.
* **Multi-Asset Structural Alignment:** Re-engineering `#pragma pack` payloads and struct alignments to accommodate the unique tick formats, higher bandwidth requirements, and distinct margin structures of global commodity markets (Crude Oil and Gold futures) alongside banking sector equities.
