#include "simpleRingBuffer.h"
#include <iostream>
#include <chrono>


int main() {
    simpleRingBuffer<marketUpdate, 1000> networkQueue;
    marketUpdate incoming_packet {2704.20, 100, 'S'};

    const size_t ITERATIONS = 10'000'000; // Run it 10 Million times!

    auto startTime = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < ITERATIONS; ++i) {
        networkQueue.push(incoming_packet);
        marketUpdate popped_packet = networkQueue.pop();
        
        // Prevent the compiler from optimizing away the loop completely
        asm volatile("" : "+r" (popped_packet));
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();

    std::cout << "Total time for 10M operations: " << totalDuration << " ns\n";
    std::cout << "Average time per Push/Pop pair: " << (double)totalDuration / ITERATIONS << " ns\n";

    return 0;
}