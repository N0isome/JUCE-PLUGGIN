#pragma once
#include "PluginProcessor.h"

class CaptureLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void setLightMode(bool);
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override;
private:
    bool lightMode = false;
};

class StereoPeakMeter final : public juce::Component
{
public:
    void update(std::array<float, 2>, float);
    void paint(juce::Graphics&) override;
    void setLightMode(bool b) { lightMode = b; repaint(); }
private:
    bool lightMode = false;
    std::array<float, 2> levelDb { -90.0f, -90.0f }, clipSeconds {};
};

class SampleWaveform final : public juce::Component
{
public:
    explicit SampleWaveform(LoopCaptureAudioProcessor& p, bool completed = false)
        : completedOnly(completed), processor(p) {}
    void update(const LoopCaptureAudioProcessor::Snapshot& s) { snapshot = s; }
    void updateScope();
    void setConnected(bool b) { connected = b; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setLightMode(bool b) { lightMode = b; repaint(); }
private:
    bool lightMode = false, completedOnly = false, connected = false;
    LoopCaptureAudioProcessor& processor;
    LoopCaptureAudioProcessor::Snapshot snapshot;
    juce::AudioBuffer<float> scope { 2, 2048 };
    int scopeFrames = 0;
    juce::File dragFile;
    bool dragStarted = false;
};

class TestChannel final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    TestChannel();
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void setLightMode(bool b) { lightMode = b; repaint(); }
    std::function<void(const juce::File&)> onPlay;
    juce::TextButton play { "Escuchar" };
    juce::File file;
private:
    bool lightMode = false, hovering = false;
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
    void playSample(const juce::File&);
    LoopCaptureAudioProcessor& processor;
    CaptureLookAndFeel lookAndFeel;
    juce::Slider inputGain, gainRange;
    juce::TextButton recordButton { "Record" }, connectButton { "Conectar" };
    juce::ToggleButton monitorButton { "Monitorear" };
    juce::TextButton playButton { "Escuchar" }, saveButton { "Guardar como" };
    juce::TextButton revealButton { "Descargar WAV" }, themeButton { "Modo claro" };
    juce::ComboBox viewSelect, sourceSelect, deviceSelect;
    juce::Label statusLabel, timeLabel, sampleName, sampleMeta;
    StereoPeakMeter meter;
    SampleWaveform waveform, samplePreview;
    TestChannel testChannel;
    std::unique_ptr<juce::FileChooser> saveChooser;
    bool lightMode = false, mesaView = false, saving = false, connected = false;
    juce::String exportMessage;
    juce::Rectangle<int> consoleArea, gainArea, mainArea;
    int sampleTop = 400, footerTop = 774;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment, rangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorAttachment;
    double lastTickMs = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopCaptureAudioProcessorEditor)
};
