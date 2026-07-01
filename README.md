# Low-Latency HFT Pipeline Project

A high-performance, ultra-low-latency engineering sandbox designed to simulate core components of a High-Frequency Trading (HFT) execution engine.

---

## 🏗️ Architecture Modules

As this project grows, core performance blocks are organized below:

### 1. Single-Producer Single-Consumer (SPSC) Ring Buffer
* **Status:** Complete 🏁
* **Core Optimization:** Lock-free, atomic index tracking using explicit memory ordering constraints (`acquire`/`release`) to bypass heavy OS mutex locks.
* **Math Hack:** Leverages bitwise `AND` masks (`&`) for instant $O(1)$ index wrapping, completely eliminating CPU-intensive division instructions.

### 2. Custom Fixed-Size Memory Pool
* **Status:** Complete 🏁
* **Core Optimization:** Bypassing `malloc` and the OS heap entirely to guarantee predictable $O(1)$ allocation times without runtime latency spikes.
* **Concurrency Engine:** Built using hardware-level Compare-And-Swap (`compare_exchange_weak`) retry loops to allow completely lock-free concurrent allocation and deallocation across multiple threads.
* **Memory Controls:** Fine-tuned utilizing `std::memory_order_release` barriers to enforce strict instruction sequencing—guaranteeing placement new data writes are completely visible in RAM before pointers change—alongside `std::memory_order_relaxed` for maximum velocity on loop retries.
* **Safety Primitives:** Hardened against high-contention edge cases using short-circuit null evaluation guards (`current_spot != nullptr`) to eliminate risk of null-pointer dereferencing (`Segmentation faults`) during rapid pool exhaustion.

---

## ⚙️ Hardware & Environment Notes

Because low-latency code behaves uniquely depending on underlying silicon layout, performance characteristics are tracked against the host machine:

* **CPU:** Hybrid Architecture (6 Performance Cores, 4 Efficiency Cores)
* **Optimization Highlights:** Cache line isolation (`alignas(64)`) implemented via custom node unions to eliminate false sharing between neighboring core execution lines.

---
