#include "PluginProcessor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
void waitFor(LoopCaptureAudioProcessor& processor, capture::State expected)
{
    for (int i = 0; i < 1000; ++i)
    {
        if (processor.getSnapshot().state == expected) return;
        juce::Thread::sleep(5);
    }
    const auto snapshot = processor.getSnapshot();
    std::cerr << snapshot.message << '\n';
    require(false, "worker state deadline");
}
void setParameter(LoopCaptureAudioProcessor& processor, const char* name, float value)
{
    auto* parameter = processor.parameters.getParameter(name);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::File firstFile, secondFile, overflowFile, destructionFile;
    {
        LoopCaptureAudioProcessor processor;
        juce::MidiBuffer midi;
        constexpr int block = 256;
        constexpr int blocks = 40;
        setParameter(processor, "inputGain", 6.0f);
        setParameter(processor, "monitor", 0.0f);
        processor.prepareToPlay(48000.0, block);
        require(processor.startRecording(), "start take");
        waitFor(processor, capture::State::recording);
        juce::AudioBuffer<float> buffer(2, block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int s = 0; s < block; ++s)
            {
                buffer.setSample(0, s, 0.125f);
                buffer.setSample(1, s, -0.25f);
            }
            processor.processBlock(buffer, midi);
            require(buffer.getMagnitude(0, block) < 1.0e-8f, "monitor off is silent");
        }
        const auto peaks = processor.consumePeaks();
        const auto gain = juce::Decibels::decibelsToGain(6.0f);
        require(std::abs(peaks[1] - 0.25f * gain) < 1.0e-6f, "pre-mute gain/peak");
        processor.stopRecording(); // No more callbacks: Stop must STILL finish.
        waitFor(processor, capture::State::ready);
        auto snapshot = processor.getSnapshot();
        require(snapshot.hasUsableFile, "closed file exported");
        require(snapshot.frames == static_cast<std::uint64_t>(block * blocks), "exact WAV frames");
        firstFile = snapshot.file;
        juce::WavAudioFormat format;
        auto stream = firstFile.createInputStream();
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
        require(reader != nullptr, "valid WAV header after Stop");
        require(reader->bitsPerSample == 24 && reader->numChannels == 2
                 && std::abs(reader->sampleRate - 48000.0) < 0.01 && !reader->usesFloatingPointData,
                 "24-bit PCM stereo");
        require(reader->lengthInSamples == block * blocks, "reader length");
        juce::AudioBuffer<float> saved(2, block);
        require(reader->read(&saved, 0, block, 0, true, true), "read saved audio");
        require(std::abs(saved.getSample(0, 10) - 0.125f * gain) < 2.0e-6f, "recording before mute, left");
        require(std::abs(saved.getSample(1, 10) + 0.25f * gain) < 2.0e-6f, "stereo right audio");
        reader.reset();

        // State round trip includes automatable gain and monitor, not temp WAVs.
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        setParameter(processor, "inputGain", -12.0f);
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(std::abs(processor.parameters.getRawParameterValue("inputGain")->load() - 6.0f)
                 < 1.0e-4f, "APVTS state round trip");

        require(processor.startRecording(), "second take");
        waitFor(processor, capture::State::recording);
        buffer.clear();
        processor.processBlock(buffer, midi);
        processor.releaseResources();
        waitFor(processor, capture::State::ready);
        secondFile = processor.getSnapshot().file;
        require(firstFile != secondFile && firstFile.existsAsFile(), "old drag source is preserved");
        require(!processor.startRecording(), "cannot record with released engine");
        processor.prepareToPlay(44100.0, block);
        require(processor.startRecording(), "cancel starting take");
        processor.stopRecording();
        for (int i = 0; i < 1000; ++i)
        {
            const auto s = processor.getSnapshot().state;
            if (s != capture::State::starting && s != capture::State::stopping
                && s != capture::State::recording) break;
            juce::Thread::sleep(5);
        }
        const auto cancelled = processor.getSnapshot().state;
        require(cancelled != capture::State::starting && cancelled != capture::State::stopping
                 && cancelled != capture::State::recording, "starting cancellation");

        // Force a rejection bigger than the real queue; the saved WAV must
        // contain only the earlier contiguous prefix, not later callbacks.
        processor.prepareToPlay(48000.0, block);
        require(processor.startRecording(), "overflow take");
        waitFor(processor, capture::State::recording);
        buffer.clear();
        processor.processBlock(buffer, midi);
        juce::AudioBuffer<float> oversized(2, 524289);
        oversized.clear();
        processor.processBlock(oversized, midi);
        waitFor(processor, capture::State::error);
        snapshot = processor.getSnapshot();
        require(snapshot.hasUsableFile && snapshot.frames == block
                 && snapshot.message.containsIgnoreCase("overflow"), "overflow preserves prefix");
        overflowFile = snapshot.file;

        // Processor destruction must close a live take without more callbacks.
        require(processor.startRecording(), "destruction take");
        waitFor(processor, capture::State::recording);
        processor.processBlock(buffer, midi);
        destructionFile = processor.getSnapshot().file;
    }
    juce::WavAudioFormat wav;
    auto closedStream = destructionFile.createInputStream();
    std::unique_ptr<juce::AudioFormatReader> closedReader(wav.createReaderFor(closedStream.release(), true));
    require(closedReader && closedReader->lengthInSamples == 256, "destructor drained and closed WAV");
    closedReader.reset();
    firstFile.deleteFile();
    secondFile.deleteFile();
    overflowFile.deleteFile();
    destructionFile.deleteFile();
    std::cout << "PASS: WAV, pre-mute gain, stereo, callback-free Stop, APVTS, release/cancel, overflow, destruction\n";
}
