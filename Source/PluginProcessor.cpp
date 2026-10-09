#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout
LoopCaptureAudioProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "inputGain", 1 }, "Input gain",
        juce::NormalisableRange<float> { -60.0f, 18.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "monitor", 1 }, "Monitor", true));
    return layout;
}

LoopCaptureAudioProcessor::LoopCaptureAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("LoopCapture disk worker"),
      parameters(*this, nullptr, "LoopCaptureParameters", makeParameters())
{
    gainDbParameter = parameters.getRawParameterValue("inputGain");
    monitorParameter = parameters.getRawParameterValue("monitor");
    formatManager.registerBasicFormats();
    setLatencySamples(0);
    if (!startThread())
        failToStart("Cannot start the disk worker.");
}

LoopCaptureAudioProcessor::~LoopCaptureAudioProcessor()
{
    // The host must have stopped callbacks before destroying the processor.
    // Join without forced thread termination: the disk worker owns the writer
    // and must close it before thumbnail/queue/metadata members disappear.
    stopRecording();
    signalThreadShouldExit();
    notify(); // Control/destruction thread only. Never called from audio.
    stopThread(-1);
}

bool LoopCaptureAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LoopCaptureAudioProcessor::prepareToPlay(double rate, int)
{
    stopRecording(); // A host reconfiguration ends the previous take.
    deviceRate.store(rate);
    inputGain.reset(rate, 0.015);
    monitorGain.reset(rate, 0.005);
    inputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(gainDbParameter->load()));
    monitorGain.setCurrentAndTargetValue(monitorParameter->load() >= 0.5f ? 1.0f : 0.0f);
    for (auto& peak : peaks)
        peak.store(0.0f);
}

void LoopCaptureAudioProcessor::releaseResources()
{
    stopRecording();
    deviceRate.store(0.0);
}

void LoopCaptureAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const auto n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2)
    {
        buffer.clear();
        return;
    }
    if (n == 0)
        return;
    for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, n);

    inputGain.setTargetValue(juce::Decibels::decibelsToGain(gainDbParameter->load()));
    monitorGain.setTargetValue(monitorParameter->load() >= 0.5f ? 1.0f : 0.0f);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    float pLeft = 0.0f, pRight = 0.0f;
    for (int sample = 0; sample < n; ++sample)
    {
        const auto gain = inputGain.getNextValue();
        // A broken upstream plugin must not poison the WAV with NaN/Inf.
        left[sample] = std::isfinite(left[sample]) ? left[sample] * gain : 0.0f;
        right[sample] = std::isfinite(right[sample]) ? right[sample] * gain : 0.0f;
        pLeft = juce::jmax(pLeft, std::abs(left[sample]));
        pRight = juce::jmax(pRight, std::abs(right[sample]));
    }
    // Telemetry only. Bounded load/store; a simultaneous UI exchange may
    // coalesce one peak. The recording queue has strict lossless ordering.
    peaks[0].store(juce::jmax(peaks[0].load(), pLeft));
    peaks[1].store(juce::jmax(peaks[1].load(), pRight));

    {
        capture::SessionGate::ProducerScope producer(gate);
        if (producer.canCapture() && !overflow.load() && !rateChanged.load()
            && !limitReached.load() && !offlineCapture.load())
        {
            if (isNonRealtime())
                offlineCapture.store(true);
            else if (std::abs(deviceRate.load() - takeRate.load()) > 0.01)
                rateChanged.store(true);
            else
            {
                const auto frames = static_cast<std::uint64_t>(n);
                if (capturedFrames.load() + frames > maxTakeFrames.load())
                    limitReached.store(true);
                else if (queue.push(buffer.getArrayOfReadPointers(), static_cast<std::size_t>(n)))
                    capturedFrames.fetch_add(frames);
                else
                    overflow.store(true);
            }
        }
    }

    // Recording and meters are post input-gain, PRE monitoring mute.
    // Turning Monitor off never mutes the capture. Both ramps are stereo-linked.
    for (int sample = 0; sample < n; ++sample)
    {
        const auto gain = monitorGain.getNextValue();
        left[sample] *= gain;
        right[sample] *= gain;
    }
}

void LoopCaptureAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&,
                                                    juce::MidiBuffer& midi)
{
    midi.clear();
    stopRecording();
    // Zero latency effect: leave the host buffer untouched on bypass.
}

bool LoopCaptureAudioProcessor::startRecording()
{
    const std::lock_guard lock(infoMutex);
    const auto state = gate.state.load();
    if (state == State::starting || state == State::recording || state == State::stopping)
        return false;

    const auto rate = deviceRate.load();
    if (!isThreadRunning())
    {
        statusMessage = "Disk worker unavailable. Reload the plugin.";
        gate.state.store(State::error);
        return false;
    }
    if (!std::isfinite(rate) || rate < 8000.0 || rate > 384000.0 || isNonRealtime())
    {
        statusMessage = "No live audio engine. Enable the DAW input first.";
        gate.state.store(State::error);
        return false;
    }
    requestedRate = rate;
    requestedBits = 24; // Signed PCM; supported in JUCE 7 and 8.
    usableFile = false;
    takeFile = juce::File {};
    statusMessage = "Opening WAV...";
    capturedFrames.store(0);
    writtenFrames.store(0);
    overflow.store(false);
    rateChanged.store(false);
    limitReached.store(false);
    offlineCapture.store(false);
    stopRequested.store(false);
    gate.state.store(State::starting); // Publish request AFTER all its data.
    return true;
}

void LoopCaptureAudioProcessor::stopRecording() noexcept
{
    stopRequested.store(true);
    auto expected = State::recording;
    gate.state.compare_exchange_strong(expected, State::stopping);
    // Starting is cancelled by the worker, even with no future audio callback.
}

void LoopCaptureAudioProcessor::failToStart(const juce::String& reason)
{
    const std::lock_guard lock(infoMutex);
    statusMessage = reason;
    usableFile = false;
    gate.state.store(State::error);
}

void LoopCaptureAudioProcessor::beginTake()
{
    if (!gate.producerIsQuiescent())
        return;
    if (stopRequested.load())
    {
        const std::lock_guard lock(infoMutex);
        statusMessage = "Recording cancelled.";
        gate.state.store(State::idle);
        return;
    }
    double rate;
    int bits;
    {
        const std::lock_guard lock(infoMutex);
        rate = requestedRate;
        bits = requestedBits;
    }
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("LoopCapture");
    const auto result = folder.createDirectory();
    if (result.failed())
    {
        failToStart("Cannot create capture folder: " + result.getErrorMessage());
        return;
    }
    const auto file = folder.getChildFile("JUCE_PLUGGIN_" + juce::Uuid().toString() + ".wav");
    auto stream = file.createOutputStream();
    if (!stream || !stream->openedOk())
    {
        failToStart("Cannot open the temporary WAV.");
        return;
    }
    juce::WavAudioFormat wav;
    // Legacy overload retained by JUCE 7/8. Ownership transfers ONLY on success.
    writer.reset(wav.createWriterFor(stream.get(), rate, 2, bits, {}, 0));
    if (!writer)
    {
        stream.reset();
        file.deleteFile();
        failToStart("Cannot create the 24-bit stereo WAV writer.");
        return;
    }
    takeStream = stream.release();
    diskFailed = false;
    queue.resetWhileInactive();
    takeRate.store(rate);
    // 30 minutes keeps thumbnail growth bounded; also respect RIFF's 4 GiB.
    maxTakeFrames.store(juce::jmin(static_cast<std::uint64_t>(rate * 1800.0),
                                  std::uint64_t { 0xffff0000ULL } / 6));
    {
        const std::lock_guard lock(thumbnailMutex);
        thumbnail.reset(2, rate, 0);
    }
    {
        const std::lock_guard lock(infoMutex);
        takeFile = file;
        statusMessage = "Recording stereo input";
    }
    gate.state.store(stopRequested.load() ? State::stopping : State::recording);
}

bool LoopCaptureAudioProcessor::drainChunk()
{
    const auto count = static_cast<int>(queue.pop(diskBuffer.getArrayOfWritePointers(),
                                                 static_cast<std::size_t>(diskChunkFrames)));
    if (count == 0)
        return false;
    if (diskFailed)
        return true; // Retire queued data safely, but never claim it was saved.
    if (!writer || !writer->writeFromAudioSampleBuffer(diskBuffer, 0, count))
    {
        diskFailed = true;
        gate.state.store(State::stopping);
        return true;
    }
    const auto start = writtenFrames.load();
    {
        const std::lock_guard lock(thumbnailMutex);
        thumbnail.addBlock(static_cast<juce::int64>(start), diskBuffer, 0, count);
    }
    writtenFrames.fetch_add(static_cast<std::uint64_t>(count));
    return true;
}

void LoopCaptureAudioProcessor::finishTake()
{
    if (writer && !writer->flush())
        diskFailed = true;
    if (takeStream)
    {
        takeStream->flush();
        if (takeStream->getStatus().failed())
            diskFailed = true;
    }
    writer.reset(); // Finalises WAV header and closes stream on worker only.
    takeStream = nullptr;
    const std::lock_guard lock(infoMutex);
    usableFile = !diskFailed && writtenFrames.load() > 0 && takeFile.existsAsFile();
    if (diskFailed)
        statusMessage = "Disk write failed. This take cannot be exported.";
    else if (overflow.load())
        statusMessage = "Queue overflow: stopped. Saved audio is the contiguous prefix.";
    else if (rateChanged.load())
        statusMessage = "Input sample rate changed. The previous take was closed.";
    else if (offlineCapture.load())
        statusMessage = "Offline rendering detected. Live capture was stopped.";
    else if (limitReached.load())
        statusMessage = "Take limit reached. Saved audio is ready to export.";
    else if (!usableFile)
        statusMessage = "No audio received. Check the DAW input routing.";
    else
        statusMessage = "Ready. Drag the WAV into your DAW.";

    const auto failed = diskFailed || overflow.load() || rateChanged.load() || offlineCapture.load();
    gate.state.store(usableFile && !failed ? State::ready : State::error);
}

void LoopCaptureAudioProcessor::run()
{
    for (;;)
    {
        if (threadShouldExit())
            stopRecording();
        auto state = gate.state.load();
        if (state == State::starting)
            beginTake();
        state = gate.state.load();
        bool didWork = false;
        if (state == State::recording || state == State::stopping)
        {
            if (stopRequested.load() || overflow.load() || rateChanged.load()
                || limitReached.load() || offlineCapture.load())
            {
                gate.state.store(State::stopping);
                state = State::stopping;
            }
            didWork = drainChunk();
            // Check producer first, THEN queue. A last block may have arrived
            // while Stop was pressed. No dependence on another audio callback.
            if (state == State::stopping && gate.producerIsQuiescent() && queue.empty())
                finishTake();
        }
        state = gate.state.load();
        if (threadShouldExit() && state != State::starting
            && state != State::recording && state != State::stopping)
            break;
        if (!didWork)
            wait(5); // Worker polling avoids OS wakeups/locks on the audio thread.
    }
}

LoopCaptureAudioProcessor::Snapshot LoopCaptureAudioProcessor::getSnapshot() const
{
    const std::lock_guard lock(infoMutex);
    return { gate.state.load(), takeFile, statusMessage, takeRate.load(),
             writtenFrames.load(), usableFile };
}

std::array<float, 2> LoopCaptureAudioProcessor::consumePeaks() noexcept
{
    return { peaks[0].exchange(0.0f), peaks[1].exchange(0.0f) };
}

void LoopCaptureAudioProcessor::drawWaveform(juce::Graphics& graphics,
                                            juce::Rectangle<int> area, double duration)
{
    const std::lock_guard lock(thumbnailMutex);
    thumbnail.drawChannels(graphics, area, 0.0, juce::jmax(0.1, duration), 1.0f);
}

void LoopCaptureAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void LoopCaptureAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* LoopCaptureAudioProcessor::createEditor()
{
    return new LoopCaptureAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoopCaptureAudioProcessor();
}
