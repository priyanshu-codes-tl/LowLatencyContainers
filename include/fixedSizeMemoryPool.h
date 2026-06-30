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
        
        node* allocate_node = m_free_spot.load(std::memory_order_relaxed);

        if(allocate_node == nullptr) [[unlikely]] {
            return nullptr;
        }
        
        m_free_spot.store(allocate_node->next, std::memory_order_release);

        T* object_ptr = reinterpret_cast<T*>(allocate_node->data);
        
        ::new (static_cast<void*>(object_ptr)) T(std::forward<Args>(args)...);

        return object_ptr;
    }

    void deallocate(T* ptr) noexcept {
        if (ptr == nullptr) [[unlikely]] {
            return;
        }

        ptr->~T();              //will call the detructor of typename T

        node* returning_node = reinterpret_cast<node*>(ptr);

        returning_node->next = m_free_spot.load(std::memory_order_relaxed);

        m_free_spot.store(returning_node, std::memory_order_release);

        
    }

};   

#endif