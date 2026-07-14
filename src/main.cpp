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

std::atomic<bool> is_running {true};
std::atomic<bool> start_streaming {false};

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
fixedSizeMemoryPool <marketUpdate_aligned, 10000> execution_vault;

// ---The Network Producer---
void handle_incoming_network_bytes (std::span<const char> rawNetworkBuffer) noexcept{

    if (rawNetworkBuffer.size() < sizeof(marketUpdate)) [[unlikely]] {
        return;
    }

    const auto* net_pkt = reinterpret_cast<const marketUpdate*>(rawNetworkBuffer.data());

    marketUpdate_aligned update(net_pkt->symbol, net_pkt->price, net_pkt->quantity, net_pkt->side);

    while (!transport_ring.push(update)) {
        asm volatile("pause" ::: "memory");
    }
}

void run_network_producer_loop() noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    int target_core = 4;
    CPU_SET(target_core, &cpuset);

    pthread_t current_thread = pthread_self();

    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

    if (result != 0) {
        std::cerr << "Failed to pin network producer thread to CPU " << target_core << "\n";
    } else {
        std::cout << "Network Producer thread hard-locked to CPU " << target_core << "\n";
    }

    //Allocate a raw C-style buffer to act as our virtual incoming network interface card
    char mock_socket_buffer[sizeof(marketUpdate)];

    while (!start_streaming.load(std::memory_order_acquire)) {
        asm volatile ("pause" ::: "memory");
    }

    std::cout << "===---Blasting 5 test ticks into the span boundary...---===\n";
    for (std::size_t i = 0; i < 5; ++i) {
        
        uint32_t price = 25000 + (i*200);
        uint32_t quantity = 100 + (i*100);
        char side = (i % 2 == 0) ? 'B' : 'S';

        std::memcpy (&mock_socket_buffer[0], "NIFTY50\0", 8);
        std::memcpy (&mock_socket_buffer[8], &price, 4);
        std::memcpy (&mock_socket_buffer[12], &quantity, 4);
        std::memcpy (&mock_socket_buffer[16], &side, 1);

        handle_incoming_network_bytes(mock_socket_buffer);

    }

}

// ---The Strategy Consumer---
void run_strategy_consumer () noexcept {
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);      //Clear all CPU from the configuration mask

    int target_core = 2;
    CPU_SET(target_core, &cpuset);

    pthread_t current_thread = pthread_self();

    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

    if (result != 0) {
        std::cerr << "Failed to pin Strategy Consumer to CPU " << target_core << "\n";
    } else {
        std::cout << "Strategy Consumer thread hard-locked to CPU " << target_core << "\n";
    }
    
    marketUpdate_aligned localPacket;

    while (is_running.load(std::memory_order_relaxed) || !transport_ring.is_empty()) {

        if(transport_ring.pop(localPacket)) {
            
            if (localPacket.side == 'B' && localPacket.price > 25500) {
                marketUpdate_aligned* savedSignal = execution_vault.allocate(localPacket.symbol, localPacket.price, localPacket.quantity, localPacket.side);

                if (savedSignal != nullptr) {
                    std::cout << "[STRATEGY MATCH] Vaulted Signal for " 
                              << std::string_view(savedSignal->symbol.data(), savedSignal->symbol.size()) 
                              << " | Price: " << savedSignal->price 
                              << " | Qty: " << savedSignal->quantity << "\n";
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
    std::thread producer_thread(run_network_producer_loop);

    // Give the consumer a brief moment to finish its OS scheduling setup
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    start_streaming.store(true, std::memory_order_release);

    // Give the producer time to complete its 5-tick simulation blast
    producer_thread.join();


    //ShutDown Sequence
    std::cout << "===Stopping background worker loops...===\n";
    is_running.store(false, std::memory_order_relaxed);
    consumer_thread.join();

    std::cout << "===---Pipeline simulation completed successfully!---===\n";
    return 0;

}