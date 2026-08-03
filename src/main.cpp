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

static constexpr uint32_t NULL_IDX = 0xFFFFFFFF;

#pragma pack(push, 1)
struct marketUpdate {
    std::array<char, 8> symbol;
    uint64_t order_id;
    uint32_t price_tick;
    uint32_t quantity;
    char side;
};
#pragma pack(pop)

struct marketUpdate_aligned {
    std::array<char, 8> symbol;
    uint64_t order_id;
    uint32_t price_tick;
    uint32_t quantity;
    char side;

    marketUpdate_aligned() = default;

    marketUpdate_aligned (const std::array<char, 8>& sym, uint64_t oid, uint32_t p, uint32_t q, char s) : symbol(sym), order_id(oid), price_tick(p), quantity(q), side(s) {}
};

struct order_node {
    uint64_t order_id;
    uint32_t price_ticks;
    uint32_t quantity;

    uint32_t prev_idx{NULL_IDX};
    uint32_t next_idx{NULL_IDX};
};

struct price_level {
    uint32_t volume{0};
    
    uint32_t head_idx{NULL_IDX};
    uint32_t tail_idx{NULL_IDX};
};

//Order Buffer
fixedSizeMemoryPool<order_node, 100000> order_pool;
//Price Level Buffer
std::array<price_level, 1000000> bid_levels;


//Ring Buffer
lockFreeRingBuffer <marketUpdate_aligned, 512> transport_ring;


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

void add_new_order(uint64_t order_id, uint32_t price_tick, uint32_t quantity) {
    order_node* new_order_ptr = order_pool.allocate();

    if (new_order_ptr == nullptr) [[unlikely]] return;

    new_order_ptr->order_id = order_id;
    new_order_ptr->price_ticks = price_tick;
    new_order_ptr->quantity = quantity;

    new_order_ptr->prev_idx = NULL_IDX;
    new_order_ptr->next_idx = NULL_IDX;

    uint32_t new_room_index = order_pool.get_index_from_ptr(new_order_ptr);

    price_level& level = bid_levels[price_tick];

    if(level.head_idx == NULL_IDX) {
        level.head_idx = new_room_index;
        level.tail_idx = new_room_index;
    } else {
        new_order_ptr->prev_idx = level.tail_idx;

        order_node* prev_order_ptr = order_pool.get_ptr_from_index(level.tail_idx);

        prev_order_ptr->next_idx = new_room_index;

        level.tail_idx = new_room_index;
    }

    level.volume += quantity;
}

void cancel_order (uint32_t target_idx, uint32_t price_tick) {
    order_node* target_order = order_pool.get_ptr_from_index(target_idx);
    price_level& level = bid_levels[price_tick];
    
    if(target_order->prev_idx != NULL_IDX) {
        order_node* prev_order = order_pool.get_ptr_from_index(target_order->prev_idx);
        prev_order->next_idx = target_order->next_idx;
    } else {
        level.head_idx = target_order->next_idx;
    }

    if(target_order->next_idx != NULL_IDX) {
        order_node* next_order = order_pool.get_ptr_from_index(target_order->next_idx);
        next_order->prev_idx = target_order->prev_idx;
    } else {
        level.tail_idx = target_order->prev_idx;
    }

    level.volume -= target_order->quantity;

    order_pool.deallocate(target_order);
}

[[nodiscard]] uint32_t execute_aggressive_sell (uint32_t sell_quantity, uint32_t target_price_tick) {
    price_level& level = bid_levels[static_cast<std::size_t>(target_price_tick)];
    
    while (sell_quantity>0 && level.head_idx != NULL_IDX) {
        order_node* head_order = order_pool.get_ptr_from_index(level.head_idx);
        uint32_t buy_quantity = head_order->quantity;

        if(buy_quantity <= sell_quantity) {
            sell_quantity -= buy_quantity;
            level.volume -= buy_quantity;

            std::cout << "[TRADE] " << buy_quantity << " shares at Tick " 
                      << target_price_tick << ". Walking chain...\n";

            level.head_idx = head_order->next_idx;

            if(level.head_idx != NULL_IDX) {
                order_node* new_head_order = order_pool.get_ptr_from_index(level.head_idx);
                new_head_order->prev_idx = NULL_IDX;
            } else [[unlikely]] {
                level.tail_idx = NULL_IDX;
            }
            
            order_pool.deallocate(head_order);
        } else {
            head_order->quantity -= sell_quantity;
            level.volume -= sell_quantity;
            sell_quantity = 0;

            std::cout << "[TRADE] " << sell_quantity << " shares at Tick " 
                      << target_price_tick << ". Seller exhausted.\n";
        }
    }

    return sell_quantity;
}

// ---The Network Producer---
marketUpdate_aligned handle_incoming_network_bytes (std::span<const char> rawNetworkBuffer) noexcept{

    if (rawNetworkBuffer.size() < sizeof(marketUpdate)) [[unlikely]] {
        return {};
    }

    const auto* net_pkt = reinterpret_cast<const marketUpdate*>(rawNetworkBuffer.data());

    marketUpdate_aligned update(net_pkt->symbol, net_pkt->order_id, net_pkt->price_tick, net_pkt->quantity, net_pkt->side);

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

    uint32_t price_tick = 139;
    std::size_t total_processed = 0;
    std::size_t local_batch_index = 0;
    const std::size_t MAX_TICKS = 10'000'000;

    while (total_processed < MAX_TICKS) {
        uint64_t order_id = 523456780 + total_processed;
        if (price_tick<200) {
            price_tick++;
        } else {
            price_tick = 139;
        }
        uint32_t quantity = 100 + (total_processed*2);
        char side = (total_processed % 12 == 0) ? 'B' : 'S';

        std::memcpy (&mock_socket_buffer[0], "NIFTY50\0", 8);
        std::memcpy (&mock_socket_buffer[8], &order_id, 8);
        std::memcpy (&mock_socket_buffer[16], &price_tick, 4);
        std::memcpy (&mock_socket_buffer[20], &quantity, 4);
        std::memcpy (&mock_socket_buffer[24], &side, 1);

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
                if (local_batch_packet[i].side == 'B') {
                    add_new_order(local_batch_packet[i].order_id,
                                  local_batch_packet[i].price_tick,
                                  local_batch_packet[i].quantity);

                    uint32_t current_tick = local_batch_packet[i].price_tick;
                    std::cout << "[LOB UPDATE] Buy Order ID " << local_batch_packet[i].order_id 
                              << " added. Total Volume at Tick " << current_tick 
                              << ": " << bid_levels[current_tick].volume << "\n";
                }

                if (local_batch_packet[i].side == 'S') {
                    
                    local_batch_packet[i].quantity = execute_aggressive_sell(local_batch_packet[i].quantity, 
                                                                             local_batch_packet[i].price_tick);
    
                // 2. Log the leftover volume (Soon, this will be pushed to the Ask book)
                    if (local_batch_packet[i].quantity > 0) {
                        std::cout << "[MARKET ALERT] Sell Order ID " << local_batch_packet[i].order_id 
                                    << " has " << local_batch_packet[i].quantity 
                                    << " shares leftover. (Awaiting Ask Book Implementation)\n";
                    }
                }
            } 
        } else {
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