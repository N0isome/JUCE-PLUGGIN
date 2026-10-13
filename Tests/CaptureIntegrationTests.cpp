#include "PluginProcessor.h"
#include "PluginEditor.h"
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

void runSystemAudioTests();
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    runSystemAudioTests();
    juce::File firstFile, secondFile, overflowFile, destructionFile, midiFile;
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

        // Preview must reach the DAW output with monitor muted; no disk I/O is
        // performed by processBlock, and sample-rate conversion runs on worker.
        require(processor.startPreview(firstFile), "start preview");
        juce::Thread::sleep(30);
        float previewMagnitude = 0;
        for (int i = 0; i < 200 && processor.isPreviewPlaying(); ++i)
        {
            buffer.clear(); processor.processBlock(buffer, midi);
            previewMagnitude = juce::jmax(previewMagnitude, buffer.getMagnitude(0, block));
            juce::Thread::sleep(2);
        }
        require(previewMagnitude > 0.1f, "preview audible despite monitor mute");
        require(!processor.isPreviewPlaying(), "preview finishes without looping");
        require(std::abs(processor.previewPosition() - static_cast<double>(block * blocks) / 48000) < 0.001,
                "preview preserves complete duration");
        processor.stopPreview(); juce::Thread::sleep(10);
        processor.prepareToPlay(44100, block);
        require(processor.startPreview(firstFile), "preview after sample-rate change");
        juce::Thread::sleep(30);
        for (int i = 0; i < 200 && processor.isPreviewPlaying(); ++i)
        { buffer.clear(); processor.processBlock(buffer, midi); juce::Thread::sleep(2); }
        require(!processor.isPreviewPlaying(), "resampled preview stops");
        require(std::abs(processor.previewPosition() - static_cast<double>(block * blocks) / 48000) < 0.001,
                "resampling preserves playback duration");
        juce::Thread::sleep(10);
        processor.prepareToPlay(48000, block);

        // A note from a controller starts a take while the editor is closed.
        // A pedal-style CC high edge stops it; the worker owns both WAV calls.
        setParameter(processor, "midiNote", 60.0f);
        setParameter(processor, "midiCc", 20.0f);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        buffer.clear(); processor.processBlock(buffer, midi);
        require(midi.isEmpty(), "control MIDI consumed by audio effect");
        waitFor(processor, capture::State::recording);
        for (int i = 0; i < 4; ++i)
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
        }
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 20, 127), 0);
        processor.processBlock(buffer, midi);
        waitFor(processor, capture::State::ready);
        require(processor.getSnapshot().frames >= static_cast<std::uint64_t>(block * 4),
                "MIDI note and CC complete a WAV without editor or transport");
        midiFile = processor.getSnapshot().file;
        setParameter(processor, "midiNote", -1.0f);
        setParameter(processor, "midiCc", -1.0f);


        if (argc == 3 && juce::String(argv[1]) == "--render-ui")
        {
            const juce::File folder { juce::String(argv[2]) }; folder.createDirectory();
            auto render = [&](const juce::String& name, juce::AudioProcessorEditor& editor)
            {
                const auto target = folder.getChildFile(name + ".png"); target.deleteFile();
                auto png = target.createOutputStream();
                require(png != nullptr, "preview output stream");
                juce::PNGImageFormat pngFormat;
                require(pngFormat.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()), *png), "render native UI");
            };
            LoopCaptureAudioProcessor blankProcessor; blankProcessor.prepareToPlay(48000, block);
            std::unique_ptr<juce::AudioProcessorEditor> blank(blankProcessor.createEditor());
            render("juce-pluggin-rack-dark", *blank);
            for (int i = 0; i < blank->getNumChildComponents(); ++i)
                if (auto* button = dynamic_cast<juce::TextButton*>(blank->getChildComponent(i)))
                    if (button->getButtonText() == "Modo claro") button->onClick();
            render("juce-pluggin-rack-light", *blank);
            for (int i = 0; i < blank->getNumChildComponents(); ++i)
                if (auto* combo = dynamic_cast<juce::ComboBox*>(blank->getChildComponent(i)))
                    if (combo->getName() == "Interfaz de prueba") combo->setSelectedId(2, juce::sendNotificationSync);
            render("juce-pluggin-mesa-light", *blank);
            std::unique_ptr<juce::AudioProcessorEditor> ready(processor.createEditor());
            render("juce-pluggin-recorded", *ready);
        }


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
    midiFile.deleteFile();
    destructionFile.deleteFile();
    std::cout << "PASS: WAV, pre-mute gain, stereo, preview/resampling, callback-free Stop, APVTS, release/cancel, overflow, destruction\n";
}
