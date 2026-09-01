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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <endian.h>

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


//Ring Buffer
lockFreeRingBuffer <marketUpdate_aligned, 512> transport_ring;
//Order Buffer
fixedSizeMemoryPool<order_node, 100000> order_pool;
//Price Level Buffer
std::array<price_level, 1000000> bid_levels;
std::array<price_level, 1000000> ask_level;

uint32_t best_bid = 0;
uint32_t best_ask = NULL_IDX;


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

void add_new_order(uint64_t order_id, uint32_t price_tick, uint32_t quantity, char side) {
    order_node* new_order_ptr = order_pool.allocate();

    if (new_order_ptr == nullptr) [[unlikely]] return;

    new_order_ptr->order_id = order_id;
    new_order_ptr->price_ticks = price_tick;
    new_order_ptr->quantity = quantity;

    new_order_ptr->prev_idx = NULL_IDX;
    new_order_ptr->next_idx = NULL_IDX;

    uint32_t new_room_index = order_pool.get_index_from_ptr(new_order_ptr);

    price_level& level = (side == 'B') ? bid_levels[price_tick] : ask_level[price_tick];

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

    if(side=='B') {
        if(best_bid < price_tick) best_bid = price_tick;
    } else {
        if(best_ask > price_tick) best_ask = price_tick;
    }
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
        order_node* head_buy_order = order_pool.get_ptr_from_index(level.head_idx);
        uint32_t head_buy_order_quantity = head_buy_order->quantity;

        if(head_buy_order_quantity <= sell_quantity) {
            sell_quantity -= head_buy_order_quantity;
            level.volume -= head_buy_order_quantity;

        //    std::cout << "[TRADE] " << buy_quantity << " shares at Tick " 
        //              << target_price_tick << ". Walking chain...\n";

            level.head_idx = head_buy_order->next_idx;

            order_pool.deallocate(head_buy_order);

            if(level.head_idx != NULL_IDX) {
                order_node* new_head_order = order_pool.get_ptr_from_index(level.head_idx);
                new_head_order->prev_idx = NULL_IDX;
            } else [[unlikely]] {
                level.tail_idx = NULL_IDX;
            }
            
        } else {
            head_buy_order->quantity -= sell_quantity;
            level.volume -= sell_quantity;

        //    std::cout << "[TRADE] " << sell_quantity << " shares at Tick " 
        //              << target_price_tick << ". Seller exhausted.\n";

            sell_quantity = 0;
        }
    }

    return sell_quantity;
}

[[nodiscard]] uint32_t execute_aggressive_buy (uint32_t buy_quantity, uint32_t target_price_tick) {
    price_level& level = ask_level[static_cast<std::size_t>(target_price_tick)];

    while (buy_quantity>0 && level.head_idx != NULL_IDX) {

        order_node* head_sell_order = order_pool.get_ptr_from_index(level.head_idx);
        uint32_t head_sell_order_quantity = head_sell_order->quantity;

        if (head_sell_order_quantity <= buy_quantity) {
            buy_quantity -= head_sell_order_quantity;

            level.volume -= head_sell_order_quantity;
            level.head_idx = head_sell_order->next_idx;

            order_pool.deallocate(head_sell_order);

            if(level.head_idx != NULL_IDX) {
                order_node* new_head_sell_order = order_pool.get_ptr_from_index(level.head_idx);
                new_head_sell_order->prev_idx = NULL_IDX;
            } else [[unlikely]] {
                level.tail_idx = NULL_IDX;
            }
        } else {
            head_sell_order->quantity -= buy_quantity;
            level.volume -= buy_quantity;
            buy_quantity = 0;
        }
    }


    return buy_quantity;
}

[[nodiscard]] uint32_t sweep_bid (uint32_t sell_quantity) {
    
    while (sell_quantity>0 && best_bid>0) {
        price_level& current_best_level = bid_levels[static_cast<std::size_t>(best_bid)];

        if(current_best_level.head_idx != NULL_IDX) {
            sell_quantity = execute_aggressive_sell(sell_quantity, best_bid);
        }

        if(sell_quantity > 0) {
            best_bid -= 5;
        }
    }

    return sell_quantity;
}

[[nodiscard]] uint32_t sweep_ask (uint32_t buy_quantity) {

    while(buy_quantity>0 && best_ask<1000000) {
        price_level& current_best_level = ask_level[static_cast<std::size_t>(best_ask)];

        if(current_best_level.head_idx != NULL_IDX) {
            buy_quantity = execute_aggressive_buy(buy_quantity, best_ask);
        }

        if(buy_quantity>0) {
            best_ask += 5;
        }
    }

    return buy_quantity;
}

// ---The Network Producer---
marketUpdate_aligned handle_incoming_network_bytes (std::span<const char> rawNetworkBuffer) noexcept {

    if (rawNetworkBuffer.size() < sizeof(marketUpdate)) [[unlikely]] {
        return {};
    }

    const auto* net_pkt = reinterpret_cast<const marketUpdate*>(rawNetworkBuffer.data());

    uint64_t safe_order_id = be64toh(net_pkt->order_id);
    uint32_t safe_price = ntohl(net_pkt->price_tick);
    uint32_t safe_quantity = ntohl(net_pkt->quantity);

    marketUpdate_aligned update(net_pkt->symbol,
                                safe_order_id,
                                safe_price,
                                safe_quantity,
                                net_pkt->side);

    return update;    
}

void run_network_producer() noexcept {
    pin_thread(4, "producer thread");

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0) return;

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(12345);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr));

    ip_mreq group{};
    group.imr_multiaddr.s_addr = inet_addr("239.255.0.1");
    group.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group));

    alignas(64) char network_buffer[2048];


    constexpr std::size_t batch_size = 8;
    marketUpdate_aligned network_batch_packets[batch_size];
    std::size_t local_batch_index = 0;

    while (hold.load(std::memory_order_relaxed)) {
        asm volatile ("pause" ::: "memory");
    }

    std::cout << "===---Listening to Live UDP Multicast Stream...---===\n";

    while (market_open.load(std::memory_order_relaxed)) {
        ssize_t bytes_received = recvfrom(sock, network_buffer, sizeof(network_buffer), 0, nullptr, nullptr);

        if(bytes_received >= sizeof(marketUpdate)) {
            std::span<const char> raw_span(network_buffer, bytes_received);
            network_batch_packets[local_batch_index] = handle_incoming_network_bytes(raw_span);
            local_batch_index++;

            if(local_batch_index == batch_size) {
                while(transport_ring.push_batch(network_batch_packets, batch_size) == 0) {
                    asm volatile ("pause" ::: "memory");
                }

                local_batch_index = 0;
            }

        }
    }

    close(sock);
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
                    uint32_t leftover_buy_quantity = sweep_ask(local_batch_packet[i].quantity);

                    if(leftover_buy_quantity > 0) {
                        add_new_order(local_batch_packet[i].order_id,
                                    local_batch_packet[i].price_tick,
                                    leftover_buy_quantity,
                                    'B');
                    }
                    
                  uint32_t current_tick = local_batch_packet[i].price_tick;
                    std::cout << "[LOB UPDATE] Buy Order ID " << local_batch_packet[i].order_id 
                              << " added. Total Volume at Tick " << current_tick 
                              << ": " << bid_levels[current_tick].volume << "\n"; 
                }

                if (local_batch_packet[i].side == 'S') {
                    uint32_t leftover_sell_quantity = sweep_bid(local_batch_packet[i].quantity);

                    if(leftover_sell_quantity > 0) {
                        add_new_order(local_batch_packet[i].order_id,
                                      local_batch_packet[i].price_tick,
                                      leftover_sell_quantity,
                                      'S');
                    }
    
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