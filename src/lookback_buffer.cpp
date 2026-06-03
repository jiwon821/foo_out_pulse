#include "stdafx.h"
#include "lookback_buffer.h"

lookback_buffer::lookback_buffer()
{
    buffer = std::make_unique<uint8_t>();
    buffer_size = 0;
    head = 0;
    lookback = 0;
    shared_buffer = std::make_shared<uint8_t>();
}

void lookback_buffer::queue(void *data, size_t n)
{
    size_t available_size;

    // this would raise some foobar2000 bug handling
    pfc::dynamic_assert(n <= buffer_size);
    std::lock_guard<std::mutex> lock(buffer_mutex);

    // copy as much data as we can
    available_size = buffer_size - head;
    memcpy(buffer.get() + head, data, pfc::min_t(n, available_size));

    // so in this case we mercilessly overwrite the head area
    if (available_size < n)
    {
        memcpy(buffer.get(), (uint8_t *)data + available_size, n - available_size);
    }

    head = (head + n) % buffer_size;
    lookback = pfc::min_t(buffer_size, lookback + n);
}

size_t lookback_buffer::read_back(size_t n)
{
    size_t read_size;
    size_t offset;

    std::lock_guard<std::mutex> lock(buffer_mutex);

    read_size = pfc::min_t(n, lookback);
    offset = read_size - head;

    if (head < read_size)
    {
        // copy offset bytes from the end and then copy bytes from the start
        memcpy(shared_buffer.get(), buffer.get() + buffer_size - offset, offset);
        memcpy(shared_buffer.get() + offset, buffer.get(), head);
    }
    else
    {
        // uhh, surely the offset cannot be negative so this is probably for when head == read_size
        // TODO: check what's up
        memcpy(shared_buffer.get(), buffer.get() + offset, read_size);
    }

    // reset offsets
    head = 0;
    lookback = 0;
    return to_read;
}

void lookback_buffer::reset(size_t n)
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    head = 0;
    lookback = 0;
    buffer_size = n;
    buffer = std::make_unique<uint8_t>(n);
    shared_buffer = std::make_shared<uint8_t>(n);
}
