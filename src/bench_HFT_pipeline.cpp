#include <lockFreeRingBuffer.h>
#include <fixedSizeMemoryPool.h>

#include <cstdint>
#include <span>
#include <array>
#include <atomic>
#include <thread>
#include <pthread.h>

std::atomic<bool> market_open{true};
std::atomic<bool> hold{true};

struct market_update_aligned {
    std::array<char, 8> symbol;
    uint32_t price;
    uint32_t quantity;
    char side;

    market_update_aligned() = default;
    market_update_aligned(std::array<char, 8> sym, uint32_t p, uint32_t q, char s) : symbol(sym), price(p), quantity(q), side(s) {}
};

lockFreeRingBuffer<market_update_aligned, 4096> transport_ring;
fixedSizeMemoryPool<market_update_aligned, 11'000'000> execution_vault;

void pin_thread (int core, std::string thread_name) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    CPU_SET(core, &cpuset);

    pthread_t current_thread = pthread_self();

    if(pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset)){
        std::cerr << "Failed to pin " << thread_name << " to core " << core << "\n";
    }

    else {
        std::cout << thread_name << " is succesfully locked to core " << core << "\n";
    }
}

const size_t total_operations = 10'000'000;

void run_network_producer() noexcept {

    pin_thread(4, "producer thread");

    market_update_aligned network_packet{{"NIFTY50"}, 25580, 2000, 'B'};

    while (hold.load(std::memory_order_relaxed)) {
        asm volatile ("pause" ::: "memory");
    }
    
    for (size_t i = 0; i < total_operations; ++i) {
        while (!transport_ring.push(network_packet)) {
            asm volatile ("pause" ::: "memory");
        }
    }
    
}

void run_consumer_strategy() noexcept {
    pin_thread(2, "consumer thread");

    market_update_aligned local_packet;

    while (market_open.load(std::memory_order_relaxed) || !transport_ring.is_empty()) {
        
        if (transport_ring.pop(local_packet)) {

            if (local_packet.price > 25500 && local_packet.side == 'B') {
                market_update_aligned* saved_signal = execution_vault.allocate(local_packet.symbol, local_packet.price, local_packet.quantity, local_packet.side);
            }

        }

        else {
            asm volatile ("pause" ::: "memory");
        }

    }
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "   Starting Multi-Threaded HFT Pipeline Benchmark\n";
    std::cout << "   Blasting: " << total_operations << " operations across isolated cores\n";
    std::cout << "=======================================================\n";

    std::thread consumer_thread (run_consumer_strategy);
    std::thread producer_thread (run_network_producer);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto start_time = std::chrono::high_resolution_clock::now();

    hold.store(false, std::memory_order_release);

    producer_thread.join();

    market_open.store(false, std::memory_order_relaxed);

    consumer_thread.join();

    auto end_time = std::chrono::high_resolution_clock::now();

    auto total_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

    std::cout << "\n    --- PIPELINE BENCHMARK RESULTS ---     \n";
    std::cout << "Total execution timeframe: " << total_duration << " ns\n";
    std::cout << "Average Latency per tick : " << double(total_duration) / total_operations << " ns\n";
    std::cout << "=======================================================\n";

    return 0;
}