#pragma once
#include <JuceHeader.h>
#include "CaptureCore.h"
#include <mutex>

namespace systemaudio
{
// Explicit portable decoder, also exercised without Windows audio hardware.
struct Format
{
    int channels = 0, bits = 0, stride = 0;
    bool floatingPoint = false;
    bool valid() const noexcept;
};
bool decode(const void*, int frames, Format, float* left, float* right, bool silent) noexcept;

class Capture final : private juce::Thread
{
public:
    struct Device { juce::String id, name; };
    struct Status { bool active = false, failed = false; juce::String message; };
    Capture();
    ~Capture() override;
    static bool supported() noexcept;
    static std::vector<Device> devices(juce::String& error); // control thread only
    bool start(const juce::String& id); // empty ID resolves current default on connect
    void stop(); // control thread, joins; never from processBlock
    void requestStop() noexcept;
    Status status() const; // UI only
    bool ready() const noexcept;
    bool hasFailed() const noexcept { return failed.load(); }
    // One audio-thread consumer. Does not allocate, lock, wait or call Windows.
    void render(juce::AudioBuffer<float>&, double hostRate) noexcept;
#if LOOPCAPTURE_TESTING
    void testBegin(double sampleRate);
    bool testPush(const juce::AudioBuffer<float>& b) { return queue.push(b.getArrayOfReadPointers(), static_cast<std::size_t>(b.getNumSamples())); }
    void testFail() { setMessage("Simulated device loss", true); consumerGate.state.store(capture::State::idle); }
#endif
private:
    void run() override;
    void captureDevice();
    void setMessage(const juce::String&, bool failed);
    capture::StereoRing queue { 524288 };
    capture::SessionGate consumerGate;
    std::atomic<double> rate { 0 };
    std::atomic<unsigned> generation { 0 };
    unsigned consumerGeneration = 0; // audio only
    bool primed = false; // audio only
    int pending = 0; // audio only
    juce::AudioBuffer<float> input { 2, 8192 }; // audio only
    std::array<juce::LagrangeInterpolator, 2> interpolators;
    // Fixed-storage 8th order Butterworth anti-alias filter before downsampling.
    std::array<std::array<double, 5>, 4> filterCoefficients {};
    std::array<std::array<std::array<double, 2>, 4>, 2> filterState {};
    double lastHostRate = 0;
    bool filtering = false;
    std::atomic<bool> failed { false };
    juce::String deviceId; // set before starting thread
    mutable std::mutex mutex;
    juce::String message;
};
}
