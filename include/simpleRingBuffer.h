#ifndef SIMPLERINGBUFFER_H
#define SIMPLERINGBUFFER_H

#include <iostream>
#include <array>

template <typename T, size_t SIZE>

class simpleRingBuffer {
    private:
    std::array <T, SIZE> m_buffer;          //Fixed Array
    size_t m_writer_ptr = 0;                //Writer ptr index position
    size_t m_read_ptr = 0;                  //Read ptr index position

    public:

    void push(const T &data) {                          //improvement: was passing by value changed to pass by const reference
        m_buffer[m_writer_ptr] = data;                  //Drop data into current slot
        m_writer_ptr = (m_writer_ptr + 1) % SIZE;       //Moving ptr with wrap-around
    }

    T pop() {
        T data = m_buffer[m_read_ptr];                  //Read data from current slot        
        m_read_ptr = (m_read_ptr + 1) % SIZE;           //Moving ptr with wrap around
        return data;
    }

};

struct marketUpdate {
    double price;
    int quantity;
    char action;
};

#endif