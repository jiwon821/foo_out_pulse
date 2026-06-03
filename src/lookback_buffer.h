#pragma once
// required here due to 'typedef unsigned char BYTE'
#include "stdafx.h"

#include <mutex>

// used as a buffer for rewinding, dunno much more than that yet
class lookback_buffer
{
public:
    lookback_buffer() : max_size_(0), buf_(), out_buf_(), lookback_(0)
    {
        // empty
    };

    void queue(void*, size_t);
    size_t read_back(size_t);
    void reset(size_t);
    void reset();

    std::shared_ptr<BYTE> out_buf_;

private:
    std::mutex buffer_mutex_;
    std::unique_ptr<BYTE> buf_;
    size_t max_size_;
    size_t head_ = 0;
    size_t lookback_ = 0;
};
