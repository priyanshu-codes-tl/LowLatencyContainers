#include "simpleRingBuffer.h"
#include "lockFreeRingBuffer.h"
#include <iostream>
#include <chrono>

volatile double compiler_escape_hatch = 0.0;

struct marketUpdate {
    double price;
    int quantity;
    char action;
};

int main() {
    simpleRingBuffer<marketUpdate, 1024> networkQueue;
    marketUpdate incoming_packet {2704.20, 100, 'S'};

    lockFreeRingBuffer<marketUpdate, 1024> networkQueue2;

    const size_t ITERATIONS = 10'000'000; // Run it 10 Million times!

    //Simple Ring Buffer benchmarking
    auto startTime = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < ITERATIONS; ++i) {
        networkQueue.push(incoming_packet);
        marketUpdate popped_packet = networkQueue.pop();
        
        // Prevent the compiler from optimizing away the loop completely
        compiler_escape_hatch = popped_packet.price;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();

    std::cout << "----Simple Ring Buffer Result----" << std::endl;
    std::cout << "Total time for 10M operations: " << totalDuration << " ns\n";
    std::cout << "Average time per Push/Pop pair: " << (double)totalDuration / ITERATIONS << " ns\n";

    //Lock Free Ring Buffer benchmarking
    startTime = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < ITERATIONS; i++) {
        networkQueue2.push(incoming_packet);
        marketUpdate popped_packet;
        networkQueue2.pop(popped_packet);
        
        // Prevent the compiler from optimizing away the loop completely
        compiler_escape_hatch = popped_packet.price;
    }

    endTime = std::chrono::high_resolution_clock::now();

    totalDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
    
    std::cout << "\n----Lock Ring Buffer Result----" << std::endl;
    std::cout << "Total time for 10M operations: " << totalDuration << " ns\n";
    std::cout << "Average time per Push/Pop pair: " << (double)totalDuration / ITERATIONS << " ns\n";

    return 0;
}