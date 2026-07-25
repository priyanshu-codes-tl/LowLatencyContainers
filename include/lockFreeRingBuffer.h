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


    [[nodiscard]] std::size_t push_batch (const T* data_array, std::size_t count) noexcept {
        std::size_t current_write = m_write_ptr.load(std::memory_order_relaxed);
        std::size_t current_read = m_read_ptr.load(std::memory_order_acquire);        

        std::size_t available = N - (current_write - current_read);

        std::size_t items_to_push = (count < available) ? count : available;

        if (items_to_push == 0) [[unlikely]] return 0;

        for (std::size_t i = 0; i < items_to_push; ++i) {
            m_buffer[(current_write + i) & MASK] = data_array[i];
        }
        
        m_write_ptr.store(current_write + items_to_push, std::memory_order_release);
        
        return items_to_push;
    }

    [[nodiscard]] std::size_t pop_batch (T* data_array_out, std::size_t count) noexcept {
        std::size_t current_read = m_read_ptr.load(std::memory_order_relaxed);
        std::size_t current_write = m_write_ptr.load(std::memory_order_acquire);

        std::size_t available = current_write - current_read;
        std::size_t item_to_read = (count < available) ? count : available;

        if (item_to_read == 0) [[unlikely]] return 0;

        for (std::size_t i=0; i<item_to_read; ++i) {
        data_array_out[i] = m_buffer[(current_read + i) & MASK];
        }

        m_read_ptr.store(current_read + item_to_read, std::memory_order_release);

        return item_to_read;
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return m_read_ptr.load(std::memory_order_relaxed) == m_write_ptr.load(std::memory_order_relaxed);
    }
};


#endif