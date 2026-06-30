#ifndef LOCKFREERINGBUFFER_H
#define LOCKFREERINGBUFFER_H

#include <iostream>
#include <cstddef>
#include <atomic>

template<typename T, std::size_t N>
class lockFreeRingBuffer {
    //Force the size N to be a power of 2 at compile-time!
    static_assert((N & (N - 1)) == 0, "Buffer size MUST be a power of 2!");

    private:
    //Isolate the internal data array on its own cache line
    alignas(64) T m_buffer[N];
    
    //Push write and read pointer to its own private cache line
    alignas(64) std::atomic<std::size_t> m_write_ptr{0};
    alignas(64) std::atomic<std::size_t> m_read_ptr{0};

    static constexpr std::size_t MASK = N - 1;

    public:
    //Constructor controller
    lockFreeRingBuffer() = default;
    lockFreeRingBuffer(const lockFreeRingBuffer&) = delete;
    lockFreeRingBuffer& operator= (const lockFreeRingBuffer&) = delete;


    [[nodiscard]] bool push (const T &data) noexcept {
        std::size_t current_write = m_write_ptr.load(std::memory_order_relaxed);
        std::size_t current_read = m_read_ptr.load(std::memory_order_acquire);        //Acquire read ptr state

        if (current_write - current_read >= N) [[unlikely]] {
            return false;
        }

        //Drop data using bitwise AND for instant wrap arround math
        m_buffer[current_write & MASK] = data;
        //Release the data for read pointer
        m_write_ptr.store(current_write + 1, std::memory_order_release);
        
        return true;
    }

    [[nodiscard]] bool pop(T &data_out) noexcept {
        std::size_t current_read = m_read_ptr.load(std::memory_order_relaxed);
        std::size_t current_write = m_write_ptr.load(std::memory_order_acquire);     //Acquire write ptr state
        
        if (current_read == current_write) [[unlikely]] {
            return false;
        }

        data_out = m_buffer[current_read & MASK];
        m_read_ptr.store(current_read + 1, std::memory_order_release);
        return true;
    }
};


#endif