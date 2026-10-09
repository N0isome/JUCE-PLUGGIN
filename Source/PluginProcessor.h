#pragma once
#include <JuceHeader.h>
#include "CaptureCore.h"
#include <array>
#include <mutex>

class LoopCaptureAudioProcessor final : public juce::AudioProcessor,
                                        private juce::Thread
{
public:
    using State = capture::State;
    struct Snapshot
    {
        State state = State::idle;
        juce::File file;
        juce::String message;
        double sampleRate = 0.0;
        std::uint64_t frames = 0;
        bool hasUsableFile = false;
    };

    LoopCaptureAudioProcessor();
    ~LoopCaptureAudioProcessor() override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    // startRecording/getSnapshot: control/UI threads only. Stop is atomic-only
    // and is also safe from releaseResources. Opening/closing stays on worker.
    bool startRecording();
    void stopRecording() noexcept;
    Snapshot getSnapshot() const;
    std::array<float, 2> consumePeaks() noexcept;
    void drawWaveform(juce::Graphics&, juce::Rectangle<int>, double duration);

    juce::AudioProcessorValueTreeState parameters;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JUCE PLUGGIN"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    void run() override;
    void beginTake(); // worker only
    bool drainChunk();
    void finishTake();
    void failToStart(const juce::String&);

    static constexpr std::size_t queueFrames = 524288; // 4 MiB stereo float
    static constexpr int diskChunkFrames = 8192;
    capture::SessionGate gate;
    capture::StereoRing queue { queueFrames };
    juce::AudioBuffer<float> diskBuffer { 2, diskChunkFrames };
    std::unique_ptr<juce::AudioFormatWriter> writer; // worker-owned exclusively
    juce::FileOutputStream* takeStream = nullptr; // non-owning; writer owns it
    std::atomic<float>* gainDbParameter = nullptr;
    std::atomic<float>* monitorParameter = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monitorGain;
    std::array<std::atomic<float>, 2> peaks { 0.0f, 0.0f };
    std::atomic<double> deviceRate { 0.0 }, takeRate { 0.0 };
    std::atomic<std::uint64_t> capturedFrames { 0 }, writtenFrames { 0 }, maxTakeFrames { 0 };
    std::atomic<bool> stopRequested { false }, overflow { false }, rateChanged { false };
    std::atomic<bool> limitReached { false }, offlineCapture { false };

    // These locks are NEVER acquired from processBlock/releaseResources.
    mutable std::mutex infoMutex;
    juce::File takeFile;
    juce::String statusMessage;
    bool usableFile = false;
    double requestedRate = 0.0;
    int requestedBits = 24;
    bool diskFailed = false; // worker only
    std::mutex thumbnailMutex;
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopCaptureAudioProcessor)
};
