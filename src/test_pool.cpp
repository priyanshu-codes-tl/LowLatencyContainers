#include <iostream>
#include <cassert>
#include <cstddef>
#include "fixedSizeMemoryPool.h"

// 1. A lightweight struct to test object creation and value placement
struct marketUpdate {
    double price;
    int quantity;
    char side;

    marketUpdate(double p, int q, char s) : price(p), quantity(q), side(s) {}
};

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "[Test Studio] Running Fixed-Size Memory Pool Tests" << std::endl;
    std::cout << "=================================================" << std::endl;

    // Create a tiny pool of just 3 slots so we can easily force exhaustion limits
    static constexpr std::size_t TEST_SIZE = 3;
    fixedSizeMemoryPool<marketUpdate, TEST_SIZE> pool;

    // ---------------------------------------------------------
    // TEST 1: Allocation & Data Integrity
    // ---------------------------------------------------------
    std::cout << "[Running Test 1] Allocating slots and checking data..." << std::endl;
    
    marketUpdate* p1 = pool.allocate(24600.50, 100, 'B');
    marketUpdate* p2 = pool.allocate(24601.00, 250, 'S');
    marketUpdate* p3 = pool.allocate(24602.25, 50, 'B');

    // Make sure nothing returned nullptr
    assert(p1 != nullptr && "Test 1 Failed: Allocation 1 returned nullptr!");
    assert(p2 != nullptr && "Test 1 Failed: Allocation 2 returned nullptr!");
    assert(p3 != nullptr && "Test 1 Failed: Allocation 3 returned nullptr!");

    // Verify value placement integrity
    assert(p1->price == 24600.50 && p1->quantity == 100 && p1->side == 'B');
    assert(p2->price == 24601.00 && p2->quantity == 250 && p2->side == 'S');
    assert(p3->price == 24602.25 && p3->quantity == 50  && p3->side == 'B');
    
    std::cout << "  -> PASS: All objects initialized with perfect data integrity." << std::endl;

    // ---------------------------------------------------------
    // TEST 2: Pool Exhaustion Limits
    // ---------------------------------------------------------
    std::cout << "[Running Test 2] Testing pool limit safety..." << std::endl;
    
    // The pool only has 3 slots, and we used all 3. The 4th allocation MUST fail.
    marketUpdate* p4 = pool.allocate(24605.00, 10, 'S');
    assert(p4 == nullptr && "Test 2 Failed: Pool allowed allocation beyond its capacity limit!");
    
    std::cout << "  -> PASS: Pool safely returned nullptr when full." << std::endl;

    // ---------------------------------------------------------
    // TEST 3: Deallocation and Memory Recycling
    // ---------------------------------------------------------
    std::cout << "[Running Test 3] Deallocating a slot and verifying recycling..." << std::endl;
    
    // Return slot 2 back to the pool
    pool.deallocate(p2);

    // Now try allocating again. The pool should give us back a valid slot (the one we just freed)
    marketUpdate* p_recycled = pool.allocate(24700.00, 999, 'B');
    assert(p_recycled != nullptr && "Test 3 Failed: Failed to allocate recycled slot!");
    assert(p_recycled->price == 24700.00 && p_recycled->quantity == 999 && p_recycled->side == 'B');
    
    std::cout << "  -> PASS: Slot successfully recycled, destructed, and reallocated." << std::endl;

    std::cout << "\n=================================================" << std::endl;
    std::cout << "------ALL TESTS PASSED SUCCESSFULLY!------" << std::endl;
    std::cout << "=================================================" << std::endl;

    return 0;
}