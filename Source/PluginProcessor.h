#pragma once
#include <JuceHeader.h>
#include "CaptureCore.h"
#include "SystemAudio.h"
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
    int consumeScope(juce::AudioBuffer<float>&) noexcept;
    bool startPreview(const juce::File&); // control thread; reader stays on worker
    void stopPreview() noexcept;
    bool isPreviewPlaying() const noexcept;
    double previewPosition() const noexcept;
    bool connectInput(bool system, const juce::String& deviceId = {});
    void disconnectInput(); // UI/control thread only
    bool usesSystemInput() const noexcept { return systemInput.load(); }
    systemaudio::Capture::Status systemInputStatus() const { return systemCapture.status(); }
    juce::String getSystemDeviceId() const;
#if LOOPCAPTURE_TESTING
    systemaudio::Capture& testConnectSystem(double rate) { stopRecording(); systemInput.store(true); systemCapture.testBegin(rate); return systemCapture; }
#endif
    void drawWaveform(juce::Graphics&, juce::Rectangle<int>, double duration);

    juce::AudioProcessorValueTreeState parameters;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JUCE PLUGGIN"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return systemInput.load() ? 1800.0 : 0.0; }
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

    void servicePreview(); // worker only
    systemaudio::Capture systemCapture;
    std::atomic<bool> systemInput { false };
    capture::StereoRing scopeQueue { 8192 }; // audio producer, one editor consumer
    capture::StereoRing previewQueue { 32768 }; // worker producer, audio consumer
    capture::SessionGate previewGate; // excludes audio consumer before queue reset
    juce::AudioBuffer<float> previewDisk { 2, 8192 }, previewAudio { 2, 8192 };
    std::unique_ptr<juce::AudioFormatReaderSource> previewReader;
    std::unique_ptr<juce::ResamplingAudioSource> previewResampler;
    juce::File requestedPreviewFile; // infoMutex
    std::atomic<bool> previewEof { false };
    std::atomic<std::uint64_t> previewFrames { 0 };
    int previewPending = 0; // worker-owned chunk awaiting queue capacity
    std::uint64_t previewRemainingFrames = 0; // worker only
    double previewRate = 0.0; // worker only

    static constexpr std::size_t queueFrames = 524288; // 4 MiB stereo float
    static constexpr int diskChunkFrames = 8192;
    capture::SessionGate gate;
    capture::StereoRing queue { queueFrames };
    juce::AudioBuffer<float> diskBuffer { 2, diskChunkFrames };
    std::unique_ptr<juce::AudioFormatWriter> writer; // worker-owned exclusively
    juce::FileOutputStream* takeStream = nullptr; // non-owning; writer owns it
    std::atomic<float>* gainDbParameter = nullptr;
    std::atomic<float>* monitorParameter = nullptr;
    std::atomic<float>* midiNoteParameter = nullptr;
    std::atomic<float>* midiCcParameter = nullptr;
    std::atomic<unsigned> midiToggleRequests { 0 };
    std::array<bool, 16> midiCcPressed {}; // audio callback owns this state
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monitorGain;
    std::array<std::atomic<float>, 2> peaks { 0.0f, 0.0f };
    std::atomic<double> deviceRate { 0.0 }, takeRate { 0.0 };
    std::atomic<std::uint64_t> capturedFrames { 0 }, writtenFrames { 0 }, maxTakeFrames { 0 };
    std::atomic<bool> stopRequested { false }, overflow { false }, rateChanged { false };
    std::atomic<bool> limitReached { false }, offlineCapture { false };
    std::atomic<bool> sourceFailed { false };

    // These locks are NEVER acquired from processBlock/releaseResources.
    mutable std::mutex infoMutex;
    juce::File takeFile;
    juce::String statusMessage;
    juce::String selectedSystemDevice;
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
