#include "CaptureCore.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <latch>
#include <thread>
#include <vector>

void require(bool condition, const char* text)
{
    if (!condition) { std::cerr << "FAILED: " << text << '\n'; std::exit(1); }
}

void testWrapAndOverflow()
{
    capture::StereoRing ring(8);
    std::array<float, 12> left {}, right {}, outL {}, outR {};
    for (std::size_t i = 0; i < left.size(); ++i)
    { left[i] = static_cast<float>(i); right[i] = -left[i]; }
    const float* src[] { left.data(), right.data() };
    float* dst[] { outL.data(), outR.data() };
    require(ring.push(src, 6), "initial push");
    require(!ring.push(src, 3), "overflow rejects whole block");
    require(ring.pop(dst, 4) == 4, "partial read");
    const float* tail[] { left.data() + 6, right.data() + 6 };
    require(ring.push(tail, 6), "wrapped write to exact capacity");
    require(!ring.push(src, 1), "full queue");
    require(ring.pop(dst, 12) == 8, "wrapped read");
    for (std::size_t i = 0; i < 8; ++i)
    {
        require(outL[i] == static_cast<float>(i + 4), "left sequence survives wrap");
        require(outR[i] == -outL[i], "channels remain aligned");
    }
    require(ring.empty(), "empty after drain");
    require(ring.push(src, 8), "full-capacity push");
    ring.resetWhileInactive();
    require(ring.empty(), "inactive reset");
    require(!ring.push(src, 9), "oversized block rejected");
}

void testConcurrentStream()
{
    capture::StereoRing ring(4096);
    constexpr std::size_t total = 2000000;
    std::atomic<bool> done { false };
    std::thread producer([&]
    {
        std::array<float, 257> left {}, right {};
        const float* src[] { left.data(), right.data() };
        std::size_t position = 0;
        while (position < total)
        {
            const auto count = std::min(std::size_t { 64 } + position % 194, total - position);
            for (std::size_t i = 0; i < count; ++i)
            { left[i] = static_cast<float>(position + i); right[i] = -left[i]; }
            if (ring.push(src, count)) position += count;
            else std::this_thread::yield();
        }
        done.store(true);
    });
    std::array<float, 511> left {}, right {};
    float* dst[] { left.data(), right.data() };
    std::size_t position = 0;
    while (!done.load() || !ring.empty())
    {
        const auto count = ring.pop(dst, left.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            require(left[i] == static_cast<float>(position + i), "concurrent FIFO ordering");
            require(right[i] == -left[i], "concurrent stereo integrity");
        }
        position += count;
        if (count == 0) std::this_thread::yield();
    }
    producer.join();
    require(position == total, "exact frame count");
}

void testStopWithProducerInFlight()
{
    capture::SessionGate gate;
    capture::StereoRing ring(16);
    gate.state.store(capture::State::recording);
    std::latch entered(1), allowWrite(1);
    std::thread audio([&]
    {
        capture::SessionGate::ProducerScope scope(gate);
        require(scope.canCapture(), "producer entered recording");
        entered.count_down();
        allowWrite.wait(); // Test-only: recreate Stop racing an existing callback.
        const float l[] { 1, 2, 3 }, r[] { -1, -2, -3 };
        const float* src[] { l, r };
        require(ring.push(src, 3), "last in-flight audio block");
    });
    entered.wait();
    gate.state.store(capture::State::stopping);
    require(!gate.producerIsQuiescent(), "Stop cannot destroy resources too early");
    {
        capture::SessionGate::ProducerScope next(gate);
        require(!next.canCapture(), "new callback cannot write after Stop");
    }
    allowWrite.count_down();
    audio.join();
    require(gate.producerIsQuiescent(), "producer retires without another callback");
    float l[3] {}, r[3] {};
    float* dst[] { l, r };
    require(ring.pop(dst, 3) == 3 && l[2] == 3 && r[2] == -3, "last block drains before close");
    require(ring.empty(), "safe finalisation condition");
}

int main()
{
    testWrapAndOverflow();
    testConcurrentStream();
    testStopWithProducerInFlight();
    std::cout << "PASS: wrap, whole-block overflow, 2M stereo frames, Stop race\n";
}
