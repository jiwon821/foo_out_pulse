#include "stdafx.h"
#include "lookback_buffer.h"

void lookback_buffer::queue(void* in, size_t nbytes)
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    pfc::dynamic_assert(nbytes <= max_size_);
    size_t endspace = max_size_ - head_;
    memcpy(buf_.get() + head_, in, pfc::min_t(nbytes, endspace));
    if (endspace < nbytes) {
        memcpy(buf_.get(), (BYTE*)in + endspace, nbytes - endspace);
    }
    head_ = (head_ + nbytes) % max_size_;
    lookback_ = pfc::min_t(max_size_, lookback_ + nbytes);
}

size_t lookback_buffer::read_back(size_t distance)
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    size_t to_read = pfc::min_t(distance, lookback_);
    if (head_ < to_read) {
        memcpy(out_buf_.get(), buf_.get() + max_size_ - (to_read - head_),
            to_read - head_);
        memcpy(out_buf_.get() + to_read - head_, buf_.get(), head_);
    }
    else {
        memcpy(out_buf_.get(), buf_.get() + head_ - to_read, to_read);
    }
    head_ = 0;
    lookback_ = 0;

    return to_read;
}

void lookback_buffer::reset(size_t size)
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    head_ = 0;
    lookback_ = 0;
    buf_ = std::unique_ptr<BYTE>(new BYTE[size]);
    out_buf_ = std::shared_ptr<BYTE>(new BYTE[size]);
    max_size_ = size;
}

void lookback_buffer::reset()
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    head_ = 0;
    lookback_ = 0;
}
