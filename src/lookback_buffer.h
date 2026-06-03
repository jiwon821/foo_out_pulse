#pragma once

#include <mutex>

// used as a buffer for rewinding, dunno much more than that yet
class lookback_buffer
{
    std::unique_ptr<uint8_t> buffer;
    size_t buffer_size;
    std::mutex buffer_mutex;

    // buffer offsets
    size_t head;
    size_t lookback;

public:
    std::shared_ptr<uint8_t> shared_buffer;

    lookback_buffer();

    void queue(void *, size_t);
    size_t read_back(size_t);
    void reset(size_t);
};
