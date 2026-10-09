#pragma once
#include "PluginProcessor.h"

class CaptureLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CaptureLookAndFeel();
    void setLightMode(bool);
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle,
                          juce::Slider&) override;
private:
    bool lightMode = false;
};

class StereoPeakMeter final : public juce::Component
{
public:
    void update(std::array<float, 2> incoming, float deltaSeconds);
    void paint(juce::Graphics&) override;
    void setLightMode(bool b) { lightMode = b; repaint(); }
private:
    bool lightMode = false;
    std::array<float, 2> levelDb { -72.0f, -72.0f };
    std::array<float, 2> clipSeconds { 0.0f, 0.0f };
};

class SampleWaveform final : public juce::Component
{
public:
    explicit SampleWaveform(LoopCaptureAudioProcessor& p, bool completed = false)
        : completedOnly(completed), processor(p) {}
    void update(const LoopCaptureAudioProcessor::Snapshot& s) { snapshot = s; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setLightMode(bool b) { lightMode = b; repaint(); }
private:
    bool lightMode = false;
    bool completedOnly = false;
    LoopCaptureAudioProcessor& processor;
    LoopCaptureAudioProcessor::Snapshot snapshot;
    juce::File dragFile; // Pin the exact take selected on mouseDown.
    bool dragStarted = false;
};

class LoopCaptureAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              public juce::DragAndDropContainer,
                                              private juce::Timer
{
public:
    explicit LoopCaptureAudioProcessorEditor(LoopCaptureAudioProcessor&);
    ~LoopCaptureAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void applyTheme();
    void saveSample();
    LoopCaptureAudioProcessor& processor;
    CaptureLookAndFeel lookAndFeel;
    juce::Slider inputGain;
    juce::TextButton recordButton { "Record" };
    juce::TextButton monitorButton { "Monitor" };
    juce::TextButton revealButton { "Show WAV" };
    juce::TextButton saveButton { "Guardar como" };
    juce::TextButton themeButton { "Modo claro" };
    juce::TextButton viewButton { "Interfaz: Rack" };
    juce::Label statusLabel, timeLabel, gainLabel, hintLabel;
    StereoPeakMeter meter;
    SampleWaveform waveform;
    SampleWaveform samplePreview;
    std::unique_ptr<juce::FileChooser> saveChooser;
    bool lightMode = false, mesaView = false, saving = false;
    juce::String exportMessage;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorAttachment;
    double lastTickMs = 0.0;
    int tick = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopCaptureAudioProcessorEditor)
};
