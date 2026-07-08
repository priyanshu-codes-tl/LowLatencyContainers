#include "lockFreeRingBuffer.h"
#include "fixedSizeMemoryPool.h"

#include <atomic>
#include <cstring>
#include <cstdint>
#include <thread>
#include <array>
#include <span>
#include <chrono>

std::atomic<bool> is_running {true};

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
        std::this_thread::yield();
    }
}

// ---The Strategy Consumer---
void run_strategy_consumer () noexcept {
    marketUpdate_aligned localPacket;

    while (is_running.load(std::memory_order_relaxed)) {

        if(transport_ring.pop(localPacket)) {
            
            if (localPacket.side == 'B' && localPacket.price > 25500) {
                marketUpdate_aligned* savedSignal = execution_vault.allocate(localPacket.symbol, localPacket.price, localPacket.quantity, localPacket.side);

                if (savedSignal != nullptr) {
                    std::cout << "[STRATEGY MATCH] Vaulted Signal for " << savedSignal->symbol.data() 
                              << " | Price: " << savedSignal->price 
                              << " | Qty: " << savedSignal->quantity << "\n";
                }
            }

        }
        else {
            std::this_thread::yield();
        }
    }
    
    std::cout << "===---Strategy Consumer thread shut down cleanly.---===\n";
}

int main() {

    std::cout << "====----Launching HFT Pipeline Sandbox Simulation---====\n";

    //Start the Strategy Consumer thread background worker loop
    std::thread consumer_thread(run_strategy_consumer);

    //Allocate a raw C-style buffer to act as our virtual incoming network interface card
    char mock_socket_buffer[sizeof(marketUpdate)];

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

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    }

    // Give the consumer thread an extra moment to process outstanding items in the buffer
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    //ShutDown Sequence
    std::cout << "===Stopping background worker loops...===\n";
    is_running.store(false, std::memory_order_relaxed);
    consumer_thread.join();
    std::cout << "===---Pipeline simulation completed successfully!---===\n";
    return 0;

}