#ifndef FIXEDSIZEMEMORYPOOL_H
#define FIXEDSIZEMEMORYPOOL_H

#include <cstddef>
#include <utility>
#include <new>
#include <atomic>

template<typename T, std::size_t N>
class fixedSizeMemoryPool {
    private:
    // A trick to avoid wasting memory: A Node acts as raw storage for T when allocated,
    // or as a pointer to the next free block when it is sitting idle in the free list.
    union alignas(64) node {
        char data[sizeof(T)];
        node* next;                         //ptr to next free block
    };
    // The single, contiguous storage allocated upfront on initialization
    alignas(64) node m_storage[N];
    std::atomic<node*> m_free_spot{nullptr};

    public:

    fixedSizeMemoryPool() {
        for(std::size_t i=0; i<N-1; ++i) {
            m_storage[i].next = &m_storage[i+1];
        }

        m_storage[N-1].next = nullptr;
        m_free_spot.store(&m_storage[0], std::memory_order_release);
    }

    ~fixedSizeMemoryPool() = default;
    
    template <typename... Args>
    [[nodiscard]] T* allocate(Args&&... args) noexcept {
        
        node* current_spot = m_free_spot.load(std::memory_order_relaxed);

        if(current_spot == nullptr) [[unlikely]] {
            return nullptr;
        }
        
        while (current_spot != nullptr && !m_free_spot.compare_exchange_weak(current_spot, current_spot->next, std::memory_order_release, std::memory_order_relaxed)) {};

        if(current_spot == nullptr) [[unlikely]] {
            return nullptr;
        }

        T* object_ptr = reinterpret_cast<T*>(current_spot->data);
        
        ::new (static_cast<void*>(object_ptr)) T(std::forward<Args>(args)...);

        return object_ptr;
    }

    void deallocate(T* ptr) noexcept {
        if (ptr == nullptr) [[unlikely]] {
            return;
        }

        ptr->~T();              //will call the detructor of typename T

        node* returning_node = reinterpret_cast<node*>(ptr);

        node* current_spot = m_free_spot.load(std::memory_order_relaxed);

        do {
            returning_node->next = current_spot;
        } while (!m_free_spot.compare_exchange_weak(current_spot, returning_node, std::memory_order_release, std::memory_order_relaxed));

        
    }

};   

#endif