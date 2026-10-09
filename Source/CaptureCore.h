#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace capture
{
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

enum class State : int { idle, starting, recording, stopping, ready, error };
static_assert(std::atomic<State>::is_always_lock_free);

// One producer (processBlock), one consumer (disk worker). No allocations,
// retries or locks in push/pop. Both channels are committed together.
class StereoRing final
{
public:
    explicit StereoRing(std::size_t frames)
        : capacity(frames), data(std::make_unique<float[]>(2 * frames)) {}

    bool push(const float* const* source, std::size_t frames) noexcept
    {
        const auto w = writeIndex.load(std::memory_order_relaxed);
        const auto r = readIndex.load(std::memory_order_acquire);
        if (frames > capacity - static_cast<std::size_t>(w - r))
            return false; // Never commit a partial block.
        const auto offset = static_cast<std::size_t>(w % capacity);
        const auto first = std::min(frames, capacity - offset);
        for (std::size_t c = 0; c < 2; ++c)
        {
            auto* dst = data.get() + c * capacity;
            std::memcpy(dst + offset, source[c], first * sizeof(float));
            std::memcpy(dst, source[c] + first, (frames - first) * sizeof(float));
        }
        writeIndex.store(w + frames, std::memory_order_release);
        return true;
    }

    std::size_t pop(float* const* destination, std::size_t maxFrames) noexcept
    {
        const auto r = readIndex.load(std::memory_order_relaxed);
        const auto w = writeIndex.load(std::memory_order_acquire);
        const auto frames = std::min(maxFrames, static_cast<std::size_t>(w - r));
        const auto offset = static_cast<std::size_t>(r % capacity);
        const auto first = std::min(frames, capacity - offset);
        for (std::size_t c = 0; c < 2; ++c)
        {
            const auto* src = data.get() + c * capacity;
            std::memcpy(destination[c], src + offset, first * sizeof(float));
            std::memcpy(destination[c] + first, src, (frames - first) * sizeof(float));
        }
        readIndex.store(r + frames, std::memory_order_release);
        return frames;
    }

    bool empty() const noexcept
    {
        return readIndex.load(std::memory_order_acquire)
            == writeIndex.load(std::memory_order_acquire);
    }

    // Only the consumer, after the producer has been excluded by SessionGate.
    void resetWhileInactive() noexcept
    {
        readIndex.store(0, std::memory_order_relaxed);
        writeIndex.store(0, std::memory_order_relaxed);
    }

private:
    const std::size_t capacity;
    std::unique_ptr<float[]> data;
    alignas(64) std::atomic<std::uint64_t> writeIndex { 0 };
    alignas(64) std::atomic<std::uint64_t> readIndex { 0 };
};

// Seq-cst entry/state/exit is deliberate: after publishing stopping, the
// worker must observe every producer that could have seen recording.
// A pointer swap alone would NOT make writer destruction safe.
struct SessionGate final
{
    std::atomic<State> state { State::idle };
    std::atomic<int> producers { 0 };

    struct ProducerScope final
    {
        explicit ProducerScope(SessionGate& g) noexcept : gate(g)
        { gate.producers.fetch_add(1); }
        ~ProducerScope() { gate.producers.fetch_sub(1); }
        bool canCapture() const noexcept { return gate.state.load() == State::recording; }
        ProducerScope(const ProducerScope&) = delete;
        ProducerScope& operator=(const ProducerScope&) = delete;
        SessionGate& gate;
    };

    bool producerIsQuiescent() const noexcept { return producers.load() == 0; }
};
}
