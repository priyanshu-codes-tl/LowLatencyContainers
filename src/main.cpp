#include "lockFreeRingBuffer.h"
#include "fixedSizeMemoryPool.h"

#include <iostream>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <string_view>
#include <thread>
#include <array>
#include <span>
#include <chrono>
#include <pthread.h>

alignas(64) std::atomic<bool> market_open{true};
alignas(64) std::atomic<bool> hold{true};

#pragma pack(push, 1)
struct marketUpdate {
    std::array<char, 8> symbol;
    uint32_t price;
    uint32_t quantity;
    char side;
};
#pragma pack(pop)

struct marketUpdate_aligned {
    std::array<char, 8> symbol;
    uint32_t price;
    uint32_t quantity;
    char side;

    marketUpdate_aligned() = default;

    marketUpdate_aligned (const std::array<char, 8>& sym, uint32_t p, uint32_t q, char s) : symbol(sym), price(p), quantity(q), side(s) {}
};

//Ring Buffer
lockFreeRingBuffer <marketUpdate_aligned, 512> transport_ring;
//Memory Pool
fixedSizeMemoryPool <marketUpdate_aligned, 11000> execution_vault;

//For pinning thread to cpu core
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

// ---The Network Producer---
marketUpdate_aligned handle_incoming_network_bytes (std::span<const char> rawNetworkBuffer) noexcept{

    if (rawNetworkBuffer.size() < sizeof(marketUpdate)) [[unlikely]] {
        return {};
    }

    const auto* net_pkt = reinterpret_cast<const marketUpdate*>(rawNetworkBuffer.data());

    marketUpdate_aligned update(net_pkt->symbol, net_pkt->price, net_pkt->quantity, net_pkt->side);

    return update;    
}

void run_network_producer() noexcept {
    pin_thread(4, "producer thread");

    constexpr std::size_t batch_size = 8;

    //Allocate a raw C-style buffer to act as our virtual incoming network interface card
    char mock_socket_buffer[sizeof(marketUpdate)];

    marketUpdate_aligned network_batch_packets[batch_size];

    while (hold.load(std::memory_order_relaxed)) {
        asm volatile ("pause" ::: "memory");
    }

    std::cout << "===---Ingesting Continuous Market Stream...---===\n";

    std::size_t total_processed = 0;
    std::size_t local_batch_index = 0;
    const std::size_t MAX_TICKS = 10'000'000;

    while (total_processed < MAX_TICKS) {
        
        uint32_t price = 25000 + (total_processed*2);
        uint32_t quantity = 100 + (total_processed*2);
        char side = (total_processed % 2 == 0) ? 'B' : 'S';

        std::memcpy (&mock_socket_buffer[0], "NIFTY50\0", 8);
        std::memcpy (&mock_socket_buffer[8], &price, 4);
        std::memcpy (&mock_socket_buffer[12], &quantity, 4);
        std::memcpy (&mock_socket_buffer[16], &side, 1);

        network_batch_packets[local_batch_index] = handle_incoming_network_bytes(mock_socket_buffer);

        total_processed++;
        local_batch_index++;

        if (local_batch_index == batch_size) {
            std::size_t pushed_count = 0;

            while (pushed_count == 0) {
                pushed_count = transport_ring.push_batch(network_batch_packets, batch_size);

                if (pushed_count == 0) {
                    asm volatile ("pause" ::: "memory");
                }
            }

            local_batch_index = 0;   //reset
        }



    }

    if (local_batch_index > 0) {
        while(transport_ring.push_batch(network_batch_packets, local_batch_index) == 0) {
            asm volatile ("pause" ::: "memory");
        }
    }

}

// ---The Strategy Consumer---
void run_strategy_consumer () noexcept {
    pin_thread(2, "consumer thread");
    
    constexpr std::size_t batch_size = 8;
    marketUpdate_aligned local_batch_packet[batch_size];

    while (market_open.load(std::memory_order_relaxed) || !transport_ring.is_empty()) {
        std::size_t popped_count = transport_ring.pop_batch(local_batch_packet, batch_size);
        if(popped_count > 0) {
            for (std::size_t i=0; i<popped_count; ++i) {
            if (local_batch_packet[i].side == 'B' && local_batch_packet[i].price > 25500) {
                marketUpdate_aligned* savedSignal = execution_vault.allocate(local_batch_packet[i].symbol, 
                                                                             local_batch_packet[i].price, 
                                                                             local_batch_packet[i].quantity, 
                                                                             local_batch_packet[i].side);

                if (savedSignal != nullptr) {
                    std::cout << "[STRATEGY MATCH] Vaulted Signal for " 
                              << std::string_view(savedSignal->symbol.data(), savedSignal->symbol.size()) 
                              << " | Price: " << savedSignal->price 
                              << " | Qty: " << savedSignal->quantity << "\n";
                }
            }
        }

        }
        else {
            asm volatile ("pause" ::: "memory");
        }
    }
    
    std::cout << "===---Strategy Consumer thread shut down cleanly.---===\n";
}

int main() {

    std::cout << "====----Launching HFT Pipeline Sandbox Simulation---====\n";

    //Start the Strategy Consumer thread background worker loop
    std::thread consumer_thread(run_strategy_consumer);

    ////Start the network producer thread background worker loop
    std::thread producer_thread(run_network_producer);

    // Give the consumer a brief moment to finish its OS scheduling setup
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    hold.store(false, std::memory_order_release);

    producer_thread.join();
    
    //ShutDown Sequence
    std::cout << "===Stopping background consumer worker loops...===\n";

    market_open.store(false, std::memory_order_relaxed);

    consumer_thread.join();

    std::cout << "===---Pipeline simulation completed successfully!---===\n";
    return 0;

}