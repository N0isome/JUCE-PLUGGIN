#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

namespace
{
struct Colours
{
    juce::Colour bg, panel, field, line, text, muted, accent, onAccent, warning, shine;
};
Colours palette(bool light)
{
    if (light) return { juce::Colour(0xffeae5ee), juce::Colour(0xfff4eff7), juce::Colour(0xffddd5e6),
        juce::Colour(0xffc5b8d1), juce::Colour(0xff2d2538), juce::Colour(0xff675b76),
        juce::Colour(0xff8260b4), juce::Colour(0xfffff9ff), juce::Colour(0xff8c531d), juce::Colour(0xfffffaff) };
    return { juce::Colour(0xff201e25), juce::Colour(0xff292630), juce::Colour(0xff15131a),
        juce::Colour(0xff494151), juce::Colour(0xfff4eff9), juce::Colour(0xffb9afc6),
        juce::Colour(0xffbda4f5), juce::Colour(0xff23182f), juce::Colour(0xffefb87c), juce::Colour(0xff3a3542) };
}
juce::Font uiFont(float height, bool mono = false)
{
    static auto face = juce::Typeface::createSystemTypefaceFor(PluginBranding::SpaceGrotesk_ttf,
                                                             PluginBranding::SpaceGrotesk_ttfSize);
    static auto code = juce::Typeface::createSystemTypefaceFor(PluginBranding::IBMPlexMonoRegular_ttf,
                                                             PluginBranding::IBMPlexMonoRegular_ttfSize);
#if JUCE_MAJOR_VERSION >= 8
    return juce::Font(juce::FontOptions(mono ? code : face).withHeight(height));
#else
    return juce::Font(mono ? code : face).withHeight(height);
#endif
}
juce::String formatTime(double seconds)
{
    const auto ms = static_cast<juce::int64>(seconds * 1000.0);
    return juce::String::formatted("%02d:%02d.%03d", static_cast<int>(ms / 60000),
        static_cast<int>((ms / 1000) % 60), static_cast<int>(ms % 1000));
}
bool isBusy(capture::State s)
{
    return s == capture::State::starting || s == capture::State::recording || s == capture::State::stopping;
}
void text(juce::Graphics& g, juce::String value, juce::Rectangle<int> area,
          float size, juce::Colour colour, bool mono = false,
          juce::Justification align = juce::Justification::centredLeft)
{
    g.setFont(uiFont(size, mono)); g.setColour(colour); g.drawText(value, area, align);
}
void field(juce::Graphics& g, juce::Rectangle<float> area, const Colours& c, bool enabled = true)
{
    const auto alpha = enabled ? 1.0f : 0.43f;
    g.setGradientFill(juce::ColourGradient(c.panel.withAlpha(alpha), area.getTopLeft(),
                                         c.bg.withAlpha(alpha), area.getBottomLeft(), false));
    g.fillRoundedRectangle(area, 9.0f);
    g.setColour(c.line.withAlpha(alpha)); g.drawRoundedRectangle(area.reduced(0.5f), 9.0f, 1.0f);
    g.setColour(c.shine.withAlpha(alpha * 0.5f));
    g.drawHorizontalLine(static_cast<int>(area.getY()) + 1, area.getX() + 8, area.getRight() - 8);
}
}

void CaptureLookAndFeel::setLightMode(bool b)
{
    lightMode = b; const auto c = palette(b);
    setColour(juce::ComboBox::textColourId, c.text);
    setColour(juce::ComboBox::backgroundColourId, c.panel);
    setColour(juce::ComboBox::outlineColourId, c.line);
    setColour(juce::PopupMenu::backgroundColourId, c.panel);
    setColour(juce::PopupMenu::textColourId, c.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, c.accent);
    setColour(juce::PopupMenu::highlightedTextColourId, c.onAccent);
    setColour(juce::Label::textColourId, c.text);
}
void CaptureLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                             const juce::Colour&, bool over, bool down)
{
    auto c = palette(lightMode); if (over) c.panel = c.panel.brighter(0.09f);
    if (down) c.panel = c.panel.darker(0.1f);
    const auto area = b.getLocalBounds().toFloat();
    if (b.getComponentID() == "record")
    {
        g.setColour((b.getToggleState() ? c.warning : c.accent).withAlpha(b.isEnabled() ? 1.0f : 0.43f));
        g.fillRoundedRectangle(area, 9.0f);
    }
    else field(g, area, c, b.isEnabled());
}
void CaptureLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto c = palette(lightMode);
    text(g, b.getButtonText(), b.getLocalBounds().reduced(8), 16.0f,
         (b.getComponentID() == "record" ? c.onAccent : c.text).withAlpha(b.isEnabled() ? 1.0f : 0.43f),
         false, juce::Justification::centred);
}
void CaptureLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const auto c = palette(lightMode); auto area = b.getLocalBounds();
    const juce::Rectangle<float> box(2.0f, static_cast<float>(area.getCentreY() - 9), 18.0f, 18.0f);
    g.setColour(b.getToggleState() ? c.accent : c.panel); g.fillRoundedRectangle(box, 3);
    g.setColour(c.muted); g.drawRoundedRectangle(box, 3, 1);
    if (b.getToggleState())
    {
        juce::Path check; check.startNewSubPath(box.getX() + 4, box.getY() + 9);
        check.lineTo(box.getX() + 8, box.getY() + 13); check.lineTo(box.getX() + 15, box.getY() + 5);
        g.setColour(c.onAccent); g.strokePath(check, juce::PathStrokeType(2));
    }
    text(g, b.getButtonText(), area.withTrimmedLeft(28), 14, c.text);
}
void CaptureLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& b)
{
    const auto c = palette(lightMode); field(g, { 0, 0, static_cast<float>(w), static_cast<float>(h) }, c, b.isEnabled());
    juce::Path p; p.startNewSubPath(static_cast<float>(w - 16), static_cast<float>(h / 2 - 2));
    p.lineTo(static_cast<float>(w - 12), static_cast<float>(h / 2 + 2));
    p.lineTo(static_cast<float>(w - 8), static_cast<float>(h / 2 - 2));
    g.setColour(c.text.withAlpha(b.isEnabled() ? 1.0f : 0.43f)); g.strokePath(p, juce::PathStrokeType(1));
}
juce::Font CaptureLookAndFeel::getComboBoxFont(juce::ComboBox&) { return uiFont(15.0f); }
juce::Font CaptureLookAndFeel::getPopupMenuFont() { return uiFont(15.0f); }
void CaptureLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                         float pos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const auto c = palette(lightMode); const auto cy = static_cast<float>(y + h / 2);
    g.setColour(c.line); g.fillRect(static_cast<float>(x), cy - 2, static_cast<float>(w), 4.0f);
    g.setColour(c.accent); g.fillRect(static_cast<float>(x), cy - 2, juce::jmax(0.0f, pos - x), 4.0f);
    g.setColour(c.muted); g.fillEllipse(pos - 8, cy - 8, 16, 16);
    g.setColour(c.text); g.drawEllipse(pos - 8, cy - 8, 16, 16, 1.2f);
}
void CaptureLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                         float position, float start, float end, juce::Slider& slider)
{
    const auto c = palette(lightMode);
    const auto radius = juce::jmin(w, h) * 0.5f - 4.0f;
    const juce::Point<float> centre(static_cast<float>(x) + w * 0.5f, static_cast<float>(y) + h * 0.5f);
    const auto bounds = juce::Rectangle<float>(radius * 2, radius * 2).withCentre(centre);
    juce::Path shadow; shadow.addEllipse(bounds.reduced(6));
    juce::DropShadow(juce::Colours::black.withAlpha(0.3f), 9, { 4, 6 }).drawForPath(g, shadow);
    g.setGradientFill(juce::ColourGradient(c.panel, bounds.getTopLeft(), c.field, bounds.getBottomRight(), false));
    g.fillEllipse(bounds.reduced(6)); g.setColour(c.line); g.drawEllipse(bounds.reduced(6), 1);
    juce::Path track, active; const auto angle = start + position * (end - start);
    track.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
    active.addCentredArc(centre.x, centre.y, radius, radius, 0, start, angle, true);
    g.setColour(c.line); g.strokePath(track, juce::PathStrokeType(6));
    g.setColour(c.accent); g.strokePath(active, juce::PathStrokeType(6));
    g.drawLine(juce::Line<float>(centre.getPointOnCircumference(radius - 16, angle),
                               centre.getPointOnCircumference(radius - 26, angle)), 4);
    const auto small = w < 110; const auto valueArea = bounds.toNearestInt();
    text(g, juce::String(slider.getValue(), 1), valueArea.withY(valueArea.getCentreY() - 18).withHeight(32),
         small ? 18.0f : 26.0f, c.text, true, juce::Justification::centred);
    text(g, "dB", valueArea.withY(valueArea.getCentreY() + 12).withHeight(18), 12, c.muted, true, juce::Justification::centred);
}

void StereoPeakMeter::update(std::array<float, 2> in, float dt)
{
    for (std::size_t i = 0; i < 2; ++i)
    {
        levelDb[i] = juce::jmax(juce::Decibels::gainToDecibels(in[i], -90.0f), levelDb[i] - 26 * dt);
        clipSeconds[i] = in[i] >= 1 ? 1.0f : juce::jmax(0.0f, clipSeconds[i] - dt);
    }
    repaint();
}
void StereoPeakMeter::paint(juce::Graphics& g)
{
    const auto c = palette(lightMode); const auto muted = juce::Colour(0xffaa9bbd);
    for (int i = 0; i < 2; ++i)
    {
        const int x = i * (getWidth() / 2); const int width = getWidth() / 2;
        text(g, i == 0 ? "L" : "R", { x, 0, width, 30 }, 12, muted, true, juce::Justification::centred);
        const juce::Rectangle<float> bar(static_cast<float>(x + (width - 18) / 2), 30, 18,
                                         static_cast<float>(getHeight() - 56));
        g.setColour(juce::Colour(0xff29232f)); g.fillRoundedRectangle(bar, 3);
        const auto fill = bar.getHeight() * juce::jlimit(0.0f, 1.0f, (levelDb[static_cast<std::size_t>(i)] + 60) / 60);
        g.setColour(clipSeconds[static_cast<std::size_t>(i)] > 0 ? c.warning : c.accent);
        g.fillRoundedRectangle(bar.withY(bar.getBottom() - fill).withHeight(fill), 3);
        text(g, levelDb[static_cast<std::size_t>(i)] <= -60 ? juce::String::fromUTF8("−∞ dB")
             : juce::String(levelDb[static_cast<std::size_t>(i)], 1),
             { x - 4, getHeight() - 22, width + 8, 22 }, 10, muted, true, juce::Justification::centred);
    }
}
void SampleWaveform::updateScope() { scopeFrames = processor.consumeScope(scope); }
void SampleWaveform::paint(juce::Graphics& g)
{
    const auto c = palette(lightMode); const auto bounds = getLocalBounds();
    if (completedOnly)
    {
        g.setColour(c.field); g.fillRoundedRectangle(bounds.toFloat(), 10);
        g.setColour(c.line.withAlpha(0.5f)); g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 10, 1);
    }
    g.setColour(completedOnly ? c.line : juce::Colour(0xff332b3d));
    for (int i : { 1, 3 }) g.drawHorizontalLine(getHeight() * i / 4, 0, static_cast<float>(getWidth()));
    if (completedOnly && snapshot.frames > 0 && snapshot.hasUsableFile)
    {
        const auto duration = static_cast<double>(snapshot.frames) / snapshot.sampleRate;
        g.setColour(c.accent); processor.drawWaveform(g, bounds.reduced(1), duration);
        if (processor.isPreviewPlaying())
        {
            g.setColour(c.text);
            const auto x = static_cast<int>(processor.previewPosition() / juce::jmax(0.1, duration) * getWidth());
            g.drawVerticalLine(juce::jlimit(0, getWidth() - 1, x), 0, static_cast<float>(getHeight()));
        }
    }
    else if (!completedOnly && connected && scopeFrames > 0)
    {
        g.setColour(c.accent);
        for (int channel = 0; channel < 2; ++channel)
        {
            juce::Path p; const auto* data = scope.getReadPointer(channel);
            for (int x = 0; x < getWidth(); ++x)
            {
                const auto sample = data[juce::jmin(scopeFrames - 1, x * scopeFrames / juce::jmax(1, getWidth()))];
                const auto y = getHeight() * (channel == 0 ? 0.25f : 0.75f)
                    - juce::jlimit(-1.0f, 1.0f, sample) * getHeight() * 0.2f;
                if (x == 0) p.startNewSubPath(0, y); else p.lineTo(static_cast<float>(x), y);
            }
            g.strokePath(p, juce::PathStrokeType(1));
        }
    }
    else text(g, completedOnly ? "Sin toma finalizada" : (connected ? "Entrada del canal del DAW" : "Sin fuente conectada"),
              bounds.reduced(12), 14, completedOnly ? c.muted : juce::Colour(0xffaa9bbd), false, juce::Justification::centred);
    setMouseCursor(completedOnly && snapshot.hasUsableFile && !isBusy(snapshot.state)
                   ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}
void SampleWaveform::mouseDown(const juce::MouseEvent& e)
{
    dragStarted = false; dragFile = juce::File {};
    if (completedOnly && e.mods.isLeftButtonDown() && snapshot.hasUsableFile && !isBusy(snapshot.state)) dragFile = snapshot.file;
}
void SampleWaveform::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStarted || !dragFile.existsAsFile() || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ dragFile.getFullPathName() }, false, this);
}
void SampleWaveform::mouseUp(const juce::MouseEvent&) { dragStarted = false; dragFile = juce::File {}; }

TestChannel::TestChannel()
{
    addAndMakeVisible(play); play.setEnabled(false);
    play.onClick = [this] { if (onPlay && file.existsAsFile()) onPlay(file); };
}
void TestChannel::paint(juce::Graphics& g)
{
    const auto c = palette(lightMode); const auto area = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(hovering ? c.panel : c.field); g.fillRoundedRectangle(area, 10);
    juce::Path outline, dashed; outline.addRoundedRectangle(area, 10);
    const float dashes[] { 3, 4 }; juce::PathStrokeType(1).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(hovering ? c.accent : c.line); g.fillPath(dashed);
    text(g, "CANAL 02", { 12, 13, 120, 18 }, 12, c.muted, true);
    text(g, "Prueba de arrastre", { 12, 31, 130, 18 }, 11, c.muted, true);
    text(g, file == juce::File {} ? juce::String::fromUTF8("Suelta aquí la muestra grabada") : file.getFileName(),
         { 144, 12, getWidth() - 270, getHeight() - 24 }, 14, c.text);
}
void TestChannel::resized() { play.setBounds(getWidth() - 112, 12, 100, getHeight() - 24); }
bool TestChannel::isInterestedInFileDrag(const juce::StringArray& files)
{ return files.size() == 1 && juce::File(files[0]).hasFileExtension("wav"); }
void TestChannel::fileDragEnter(const juce::StringArray&, int, int) { hovering = true; repaint(); }
void TestChannel::fileDragExit(const juce::StringArray&) { hovering = false; repaint(); }
void TestChannel::filesDropped(const juce::StringArray& files, int, int)
{
    hovering = false;
    if (isInterestedInFileDrag(files) && juce::File(files[0]).existsAsFile()) { file = juce::File(files[0]); play.setEnabled(true); }
    repaint();
}

LoopCaptureAudioProcessorEditor::LoopCaptureAudioProcessorEditor(LoopCaptureAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), waveform(p), samplePreview(p, true)
{
    setLookAndFeel(&lookAndFeel); setOpaque(true);
    inputGain.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    inputGain.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    inputGain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gainRange.setSliderStyle(juce::Slider::LinearHorizontal); gainRange.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    inputGain.setDoubleClickReturnValue(true, 0); gainRange.setDoubleClickReturnValue(true, 0);
    inputGain.setTooltip("Ganancia de entrada: doble clic para volver a 0 dB.");
    monitorButton.setTooltip(juce::String::fromUTF8("Monitorear la entrada. Desactivarlo no silencia la grabación."));
    recordButton.setComponentID("record");
    viewSelect.addItem("Rack", 1); viewSelect.addItem("Mesa", 2); viewSelect.setSelectedId(1, juce::dontSendNotification);
    viewSelect.setName("Interfaz de prueba");
    sourceSelect.addItem("Entrada del DAW / cable", 1);
    if (systemaudio::Capture::supported()) sourceSelect.addItem("Audio sistema (Windows)", 2);
    sourceSelect.setSelectedId(processor.usesSystemInput() ? 2 : 1, juce::dontSendNotification);
    sourceSelect.setTooltip("Windows: captura la salida seleccionada. DAW: recibe el canal del Mixer.");
    sourceSelect.onChange = [this]
    {
        processor.disconnectInput(); connected = false; exportMessage.clear();
        connectButton.setButtonText("Conectar"); refreshInputDevices(); timerCallback();
    };
    deviceSelect.onChange = [this]
    {
        if (deviceSelect.getSelectedId() == 10000) { refreshInputDevices(); return; }
        if (connected) { processor.disconnectInput(); connected = false; connectButton.setButtonText("Conectar"); }
        timerCallback();
    };
    refreshInputDevices();
    for (auto* label : { &statusLabel, &sampleName, &sampleMeta }) label->setFont(uiFont(label == &sampleName ? 16.0f : 14.0f));
    sampleMeta.setFont(uiFont(12, true)); timeLabel.setFont(uiFont(19, true));
    timeLabel.setJustificationType(juce::Justification::centredRight);
    for (juce::Component* component : std::array<juce::Component*, 19> { &inputGain, &gainRange, &recordButton,
         &connectButton, &monitorButton, &playButton, &revealButton, &saveButton, &themeButton, &viewSelect,
         &sourceSelect, &deviceSelect, &statusLabel, &timeLabel, &sampleName, &sampleMeta, &meter, &waveform, &samplePreview })
        addAndMakeVisible(component);
    addAndMakeVisible(testChannel);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters, "inputGain", inputGain);
    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters, "inputGain", gainRange);
    monitorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters, "monitor", monitorButton);
    connectButton.onClick = [this]
    {
        exportMessage.clear();
        if (connected) { processor.disconnectInput(); connected = false; }
        else
        {
            const auto index = deviceSelect.getSelectedId() - 2;
            const auto id = index >= 0 && index < static_cast<int>(inputDevices.size()) ? inputDevices[static_cast<std::size_t>(index)].id : juce::String {};
            connected = processor.connectInput(sourceSelect.getSelectedId() == 2, id);
        }
        connectButton.setButtonText(connected ? "Desconectar" : "Conectar"); timerCallback();
    };
    recordButton.onClick = [this]
    {
        exportMessage.clear(); const auto s = processor.getSnapshot().state;
        if (s == capture::State::recording || s == capture::State::starting) processor.stopRecording();
        else if (connected && s != capture::State::stopping) processor.startRecording();
        timerCallback();
    };
    playButton.onClick = [this] { playSample(processor.getSnapshot().file); };
    testChannel.onPlay = [this](const juce::File& file) { playSample(file); };
    revealButton.onClick = [this] { saveSample(); };
    revealButton.setTooltip("Exportar el WAV a la carpeta que elijas.");
    saveButton.onClick = [this] { saveSample(); };
    themeButton.onClick = [this] { lightMode = !lightMode; applyTheme(); };
    viewSelect.onChange = [this] { mesaView = viewSelect.getSelectedId() == 2; resized(); repaint(); };
    connected = isBusy(processor.getSnapshot().state) || processor.systemInputStatus().active;
    connectButton.setButtonText(connected ? "Desconectar" : "Conectar");
    applyTheme();
    // Match the web rack geometry. Host DPI scales this stable logical canvas.
    setResizable(false, false); setSize(1032, 848);
    lastTickMs = juce::Time::getMillisecondCounterHiRes(); timerCallback(); startTimerHz(60);
}
LoopCaptureAudioProcessorEditor::~LoopCaptureAudioProcessorEditor()
{
    stopTimer(); processor.stopPreview(); saveChooser.reset(); monitorAttachment.reset();
    rangeAttachment.reset(); gainAttachment.reset(); setLookAndFeel(nullptr);
}
void LoopCaptureAudioProcessorEditor::timerCallback()
{
    const auto c = palette(lightMode); const auto now = juce::Time::getMillisecondCounterHiRes();
    meter.update(processor.consumePeaks(), static_cast<float>(juce::jlimit(0.001, 0.25, (now - lastTickMs) / 1000)));
    lastTickMs = now; const auto s = processor.getSnapshot(); const auto busy = isBusy(s.state);
    waveform.setConnected(connected); waveform.updateScope(); waveform.update(s); waveform.repaint();
    auto completed = s; if (busy) { completed.frames = 0; completed.hasUsableFile = false; }
    samplePreview.update(completed); samplePreview.repaint();
    const auto seconds = s.sampleRate > 0 ? static_cast<double>(s.frames) / s.sampleRate : 0;
    timeLabel.setText(formatTime(seconds), juce::dontSendNotification);
    sampleName.setText(s.hasUsableFile && !busy ? s.file.getFileName() : "Muestra grabada", juce::dontSendNotification);
    sampleMeta.setText(s.hasUsableFile && !busy ? formatTime(seconds) + juce::String::fromUTF8(" · ") + juce::String(s.sampleRate / 1000, 1) + juce::String::fromUTF8(" kHz · 24-bit PCM")
                      : juce::String::fromUTF8("La toma aparecerá aquí al pulsar Stop."), juce::dontSendNotification);
    const auto recording = s.state == capture::State::recording || s.state == capture::State::starting;
    const bool system = sourceSelect.getSelectedId() == 2;
    const auto systemStatus = processor.systemInputStatus();
    if (system && connected && systemStatus.failed)
    { connected = false; connectButton.setButtonText("Conectar"); }
    sourceSelect.setEnabled(!busy); deviceSelect.setEnabled(system && !busy);
    monitorButton.setEnabled(!system);
    recordButton.setToggleState(recording, juce::dontSendNotification);
    recordButton.setButtonText(s.state == capture::State::stopping ? juce::String::fromUTF8("Finalizando…") : recording ? "Stop" : "Record");
    recordButton.setEnabled(connected && (!system || systemStatus.active) && s.state != capture::State::stopping && processor.getSampleRate() > 0);
    const auto ready = s.hasUsableFile && !busy;
    playButton.setEnabled(ready); revealButton.setEnabled(ready); saveButton.setEnabled(ready && !saving);
    playButton.setButtonText(processor.isPreviewPlaying() ? "Detener" : "Escuchar");
    testChannel.play.setButtonText(processor.isPreviewPlaying() ? "Detener" : "Escuchar");
    testChannel.play.setEnabled(testChannel.file != juce::File {} && !busy);
    juce::String status;
    if (!exportMessage.isEmpty()) status = exportMessage;
    else if (s.state == capture::State::error) status = s.message;
    else if (s.state == capture::State::stopping) status = juce::String::fromUTF8("Finalizando archivo WAV…");
    else if (system && systemStatus.failed) status = systemStatus.message;
    else if (recording) status = system ? "Grabando audio de la salida Windows." : juce::String::fromUTF8("Grabando la entrada estéreo del canal.");
    else if (ready) status = "Muestra lista. Arrastra el WAV a otro canal del DAW.";
    else if (connected) status = system ? systemStatus.message : "Entrada del DAW conectada. Pulsa Record para capturar.";
    else status = juce::String::fromUTF8("Conecta una fuente para habilitar la grabación.");
    statusLabel.setText(status, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, s.state == capture::State::error ? c.warning : c.muted);
}
void LoopCaptureAudioProcessorEditor::refreshInputDevices()
{
    deviceSelect.clear(juce::dontSendNotification); inputDevices.clear();
    if (sourceSelect.getSelectedId() == 2)
    {
        juce::String error; inputDevices = systemaudio::Capture::devices(error);
        deviceSelect.addItem("Salida predeterminada", 1);
        for (std::size_t i = 0; i < inputDevices.size(); ++i)
            deviceSelect.addItem(inputDevices[i].name, static_cast<int>(i) + 2);
        deviceSelect.addSeparator(); deviceSelect.addItem("Actualizar dispositivos", 10000);
        if (error.isNotEmpty()) exportMessage = error;
    }
    else deviceSelect.addItem(juce::String::fromUTF8("Canal estéreo del DAW"), 1);
    int selected = 1;
    const auto id = processor.getSystemDeviceId();
    for (std::size_t i = 0; i < inputDevices.size(); ++i)
        if (inputDevices[i].id == id) selected = static_cast<int>(i) + 2;
    deviceSelect.setSelectedId(selected, juce::dontSendNotification);
}
void LoopCaptureAudioProcessorEditor::playSample(const juce::File& file)
{
    if (processor.isPreviewPlaying()) processor.stopPreview();
    else if (!processor.startPreview(file)) exportMessage = "No se pudo reproducir. Comprueba el motor de audio del DAW.";
    timerCallback();
}
void LoopCaptureAudioProcessorEditor::applyTheme()
{
    const auto c = palette(lightMode); lookAndFeel.setLightMode(lightMode);
    meter.setLightMode(lightMode); waveform.setLightMode(lightMode); samplePreview.setLightMode(lightMode); testChannel.setLightMode(lightMode);
    for (auto* label : { &statusLabel, &sampleMeta }) label->setColour(juce::Label::textColourId, c.muted);
    sampleName.setColour(juce::Label::textColourId, c.text); timeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffeae2f5));
    themeButton.setButtonText(lightMode ? "Modo oscuro" : "Modo claro"); sendLookAndFeelChange(); repaint();
}
void LoopCaptureAudioProcessorEditor::saveSample()
{
    const auto s = processor.getSnapshot(); if (saving || !s.hasUsableFile || isBusy(s.state)) return;
    const auto source = s.file;
    saveChooser = std::make_unique<juce::FileChooser>("Guardar muestra WAV",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(source.getFileName()), "*.wav");
    saving = true; saveButton.setEnabled(false); const juce::Component::SafePointer<LoopCaptureAudioProcessorEditor> safe(this);
    saveChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting, [safe, source](const juce::FileChooser& chooser)
    {
        if (!safe) return;
        const auto dest = chooser.getResult();
        if (dest == juce::File {}) { safe->saving = false; safe->timerCallback(); return; }
        const auto started = juce::Thread::launch([safe, source, dest]
        {
            const auto ok = source == dest || source.copyFileTo(dest);
            juce::MessageManager::callAsync([safe, ok, dest]
            {
                if (!safe) return;
                safe->saving = false;
                safe->exportMessage = ok ? "WAV guardado: " + dest.getFullPathName() : "No se pudo guardar el WAV. Revisa la carpeta de destino.";
                safe->timerCallback();
            });
        });
        if (!started) { safe->saving = false; safe->exportMessage = "No se pudo iniciar el guardado."; safe->timerCallback(); }
    });
}
void LoopCaptureAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto c = palette(lightMode);
    g.setGradientFill(juce::ColourGradient(c.panel, 0, 0, c.bg, static_cast<float>(getWidth()), static_cast<float>(getHeight()), false));
    g.fillAll(); g.setColour(c.line); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 22, 1);
    text(g, "JUCE", { 24, 20, 61, 34 }, 28, c.text);
    text(g, "PLUGGIN", { 83, 20, 170, 34 }, 28, c.accent);
    text(g, juce::String::fromUTF8("BY FR3DO - Demo en pañales"), { 24, 56, 440, 18 }, 11, c.muted, true);
    g.setColour(c.line); g.drawHorizontalLine(92, 0, static_cast<float>(getWidth()));
    text(g, "ENTRADA", { 24, 116, 220, 26 }, 12, c.muted, true);
    text(g, "Fuente de captura", { 24, 154, 220, 20 }, 14, c.muted);
    text(g, "Dispositivo", { 24, 237, 220, 20 }, 14, c.muted);
    text(g, "GANANCIA", { gainArea.getX(), gainArea.getY(), mesaView ? 120 : gainArea.getWidth(), 26 }, 12, c.muted, true,
         mesaView ? juce::Justification::centredLeft : juce::Justification::centred);
    if (!mesaView)
    {
        g.setColour(c.line); g.drawVerticalLine(gainArea.getX(), 116, 380); g.drawVerticalLine(gainArea.getRight(), 116, 380);
        text(g, juce::String::fromUTF8("−24"), { gainArea.getX() + 12, 317, 55, 20 }, 11, c.muted, true);
        text(g, "+12 dB", { gainArea.getRight() - 67, 317, 55, 20 }, 11, c.muted, true, juce::Justification::centredRight);
    }
    else { g.setColour(c.line); g.drawHorizontalLine(gainArea.getY() - 12, 24, static_cast<float>(getWidth() - 24)); }
    g.setColour(juce::Colour(0xff121016)); g.fillRoundedRectangle(consoleArea.toFloat(), 12);
    g.setColour(juce::Colours::black.withAlpha(0.4f)); g.drawRoundedRectangle(consoleArea.toFloat().reduced(0.5f), 12, 1);
    text(g, juce::String::fromUTF8("ENTRADA · L / R"), { consoleArea.getX() + 12, consoleArea.getY() + 12, 200, 42 }, 11, juce::Colour(0xffaa9bbd), true);
    text(g, "WAV / 24-bit PCM", { recordButton.getRight() + 12, recordButton.getY() + 3, 260, 18 }, 11, c.muted, true);
    text(g, juce::String::fromUTF8("30 min por toma · local"), { recordButton.getRight() + 12, recordButton.getY() + 20, 260, 18 }, 11, c.muted, true);
    g.setColour(c.line); g.drawHorizontalLine(sampleTop, 24, static_cast<float>(getWidth() - 24));
    text(g, "STEREO / WAV", { getWidth() - 170, sampleTop + 28, 146, 26 }, 12, c.muted, true, juce::Justification::centredRight);
    text(g, juce::String::fromUTF8("Arrastra la onda cuando esté lista."), { 24, mesaView ? 640 : sampleTop + 193, mesaView ? 550 : 330, mesaView ? 44 : 64 }, 14, c.muted);
    g.setColour(c.line); g.drawHorizontalLine(footerTop, 0, static_cast<float>(getWidth()));
    text(g, juce::String::fromUTF8("Captura local · audio no subido"), { getWidth() - 310, footerTop + 12, 286, 54 }, 11, c.accent, true, juce::Justification::centredRight);
}
void LoopCaptureAudioProcessorEditor::resized()
{
    themeButton.setBounds(getWidth() - 132, 24, 108, 44); viewSelect.setBounds(getWidth() - 248, 24, 108, 44);
    sourceSelect.setBounds(24, 181, 220, 44); deviceSelect.setBounds(24, 264, 220, 44); connectButton.setBounds(24, 322, 220, 44);
    if (!mesaView)
    {
        gainArea = { 268, 116, 172, 264 }; inputGain.setBounds(283, 142, 142, 142);
        gainRange.setBounds(280, 289, 148, 28); monitorButton.setBounds(305, 340, 116, 38);
        consoleArea = { 464, 116, getWidth() - 488, 204 }; sampleTop = 400;
        sampleName.setBounds(24, 422, getWidth() - 210, 25); sampleMeta.setBounds(24, 447, getWidth() - 210, 20);
        samplePreview.setBounds(24, 476, getWidth() - 48, 106);
        playButton.setBounds(getWidth() - 418, 594, 126, 52); saveButton.setBounds(getWidth() - 284, 594, 126, 52);
        revealButton.setBounds(getWidth() - 150, 594, 126, 52); testChannel.setBounds(24, 676, getWidth() - 48, 70);
    }
    else
    {
        consoleArea = { 268, 116, getWidth() - 292, 204 };
        gainArea = { 24, 392, getWidth() - 48, 82 }; inputGain.setBounds(164, 392, 82, 82);
        gainRange.setBounds(266, 411, getWidth() - 450, 36); monitorButton.setBounds(getWidth() - 164, 414, 140, 38);
        sampleTop = 490; sampleName.setBounds(24, 506, getWidth() - 210, 25); sampleMeta.setBounds(24, 531, getWidth() - 210, 20);
        samplePreview.setBounds(24, 558, getWidth() - 48, 70);
        playButton.setBounds(getWidth() - 418, 640, 126, 44); saveButton.setBounds(getWidth() - 284, 640, 126, 44);
        revealButton.setBounds(getWidth() - 150, 640, 126, 44); testChannel.setBounds(24, 700, getWidth() - 48, 60);
    }
    timeLabel.setBounds(consoleArea.getRight() - 223, consoleArea.getY() + 12, 113, 42);
    waveform.setBounds(consoleArea.getX() + 12, consoleArea.getY() + 54, consoleArea.getWidth() - 116, 130);
    meter.setBounds(consoleArea.getRight() - 88, consoleArea.getY() + 12, 76, 180);
    recordButton.setBounds(consoleArea.getX(), 336, 132, 44);
    footerTop = 770; statusLabel.setBounds(24, 780, getWidth() - 350, 54);
}
