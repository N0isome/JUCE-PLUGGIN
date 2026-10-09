#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

namespace
{
std::array<juce::Colour, 7> palette(bool light)
{
    if (light)
        return { juce::Colour(0xffeae5ee), juce::Colour(0xfff4eff7), juce::Colour(0xffc5b8d1),
                 juce::Colour(0xff2d2538), juce::Colour(0xff675b76), juce::Colour(0xff8260b4), juce::Colour(0xff8c531d) };
    return { juce::Colour(0xff201e25), juce::Colour(0xff292630), juce::Colour(0xff494151),
             juce::Colour(0xfff4eff9), juce::Colour(0xffb9afc6), juce::Colour(0xffbda4f5), juce::Colour(0xffefb87c) };
}

juce::Font uiFont(float height, int style = juce::Font::plain, bool mono = false)
{
    static auto displayFace = juce::Typeface::createSystemTypefaceFor(PluginBranding::SpaceGrotesk_ttf,
                                                                    PluginBranding::SpaceGrotesk_ttfSize);
    static auto monoFace = juce::Typeface::createSystemTypefaceFor(PluginBranding::IBMPlexMonoRegular_ttf,
                                                                 PluginBranding::IBMPlexMonoRegular_ttfSize);
    const auto face = mono ? monoFace : displayFace;
#if JUCE_MAJOR_VERSION >= 8
    return juce::Font(juce::FontOptions(face).withHeight(height)).withStyle(style);
#else
    return juce::Font(face).withHeight(height).withStyle(style);
#endif
}

juce::String formatTime(double seconds)
{
    const auto centiseconds = static_cast<int>(seconds * 100.0);
    return juce::String::formatted("%02d:%02d.%02d", centiseconds / 6000,
                                   (centiseconds / 100) % 60, centiseconds % 100);
}

bool isBusy(capture::State state)
{
    return state == capture::State::starting || state == capture::State::recording
        || state == capture::State::stopping;
}
}

CaptureLookAndFeel::CaptureLookAndFeel() { setLightMode(false); }

void CaptureLookAndFeel::setLightMode(bool light)
{
    lightMode = light;
    const auto [background, panel, line, ink, muted, mint, coral] = palette(lightMode);
    setColour(juce::Slider::thumbColourId, mint);
    setColour(juce::Slider::trackColourId, mint);
    setColour(juce::Slider::backgroundColourId, line);
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxBackgroundColourId, background);
    setColour(juce::Slider::textBoxOutlineColourId, line);
    setColour(juce::TextButton::buttonColourId, panel);
    setColour(juce::TextButton::buttonOnColourId, mint.withAlpha(0.18f));
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, mint);
    setColour(juce::Label::textColourId, ink);
}

void CaptureLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour&, bool highlighted, bool down)
{
    const auto [background, panel, line, ink, muted, mint, coral] = palette(lightMode);
    auto area = button.getLocalBounds().toFloat().reduced(0.5f);
    auto colour = button.findColour(button.getToggleState()
                                   ? juce::TextButton::buttonOnColourId
                                   : juce::TextButton::buttonColourId);
    if (highlighted) colour = colour.brighter(0.13f);
    if (down) colour = colour.darker(0.12f);
    g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.35f));
    g.fillRoundedRectangle(area, 12.0f);
    g.setColour(button.getToggleState() ? mint.withAlpha(0.6f) : line);
    g.drawRoundedRectangle(area, 12.0f, 1.0f);
}

void CaptureLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont(uiFont(15.0f, juce::Font::bold));
    g.setColour(button.findColour(button.getToggleState()
                                 ? juce::TextButton::textColourOnId
                                 : juce::TextButton::textColourOffId)
                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.drawText(button.getButtonText(), button.getLocalBounds().reduced(8),
               juce::Justification::centred);
}

void StereoPeakMeter::update(std::array<float, 2> incoming, float deltaSeconds)
{
    for (std::size_t c = 0; c < 2; ++c)
    {
        const auto target = juce::Decibels::gainToDecibels(incoming[c], -72.0f);
        levelDb[c] = juce::jmax(target, levelDb[c] - 26.0f * deltaSeconds);
        clipSeconds[c] = incoming[c] >= 1.0f ? 1.0f
                                           : juce::jmax(0.0f, clipSeconds[c] - deltaSeconds);
    }
    repaint();
}

void StereoPeakMeter::paint(juce::Graphics& g)
{
    const auto [background, panel, line, ink, muted, mint, coral] = palette(lightMode);
    const auto area = getLocalBounds().toFloat();
    g.setColour(panel);
    g.fillRoundedRectangle(area, 16.0f);
    g.setColour(muted);
    g.setFont(uiFont(11.0f, juce::Font::bold));
    g.drawText("INPUT / PEAK", 12, 12, getWidth() - 24, 20, juce::Justification::centred);
    const auto barTop = 52.0f;
    const auto barHeight = juce::jmax(16.0f, area.getHeight() - 96.0f);
    const auto barWidth = juce::jmax(9.0f, (area.getWidth() - 65.0f) * 0.5f);
    for (std::size_t c = 0; c < 2; ++c)
    {
        const auto x = 18.0f + static_cast<float>(c) * (barWidth + 12.0f);
        const juce::Rectangle<float> bar { x, barTop, barWidth, barHeight };
        g.setColour(background);
        g.fillRoundedRectangle(bar, 5.0f);
        const auto fillHeight = barHeight * juce::jlimit(0.0f, 1.0f, (levelDb[c] + 72.0f) / 72.0f);
        juce::ColourGradient gradient(mint, x, bar.getBottom(), coral, x, bar.getY(), false);
        gradient.addColour(0.8, juce::Colour(0xffffcd7e));
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bar.withY(bar.getBottom() - fillHeight).withHeight(fillHeight), 4.0f);
        g.setColour(clipSeconds[c] > 0.0f ? coral : line);
        g.fillRoundedRectangle(x, barTop - 10.0f, barWidth, 4.0f, 2.0f);
        g.setColour(ink);
        g.setFont(12.0f);
        g.drawText(c == 0 ? "L" : "R", static_cast<int>(x), static_cast<int>(bar.getBottom() + 9),
                   static_cast<int>(barWidth), 18, juce::Justification::centred);
    }
    g.setColour(muted);
    g.setFont(10.0f);
    for (int db : { 0, -12, -24, -48, -72 })
    {
        const auto y = barTop + barHeight * (static_cast<float>(-db) / 72.0f);
        g.drawText(juce::String(db), getWidth() - 29, static_cast<int>(y - 7), 27, 14,
                   juce::Justification::centredLeft);
    }
}

void SampleWaveform::paint(juce::Graphics& g)
{
    const auto [background, panel, line, ink, muted, mint, coral] = palette(lightMode);
    const auto area = getLocalBounds().toFloat();
    g.setColour(panel);
    g.fillRoundedRectangle(area, 16.0f);
    g.setColour(line);
    g.drawRoundedRectangle(area.reduced(0.5f), 16.0f, 1.0f);
    auto content = getLocalBounds().reduced(14);
    auto title = content.removeFromTop(26);
    g.setColour(muted);
    g.setFont(uiFont(11.0f, juce::Font::bold));
    g.drawText(completedOnly ? "MUESTRA GRABADA" : "CAPTURA EN CURSO", title, juce::Justification::centredLeft);
    const auto canDrag = snapshot.hasUsableFile && !isBusy(snapshot.state);
    g.setColour(canDrag ? mint : muted);
    g.drawText(canDrag ? "DRAG WAV TO DAW" : "STEREO  /  24-BIT PCM",
               title, juce::Justification::centredRight);
    auto footer = content.removeFromBottom(30);
    content.reduce(0, 8);
    g.setColour(line.withAlpha(0.7f));
    for (int column = 1; column < 10; ++column)
    {
        const auto x = content.getX() + content.getWidth() * column / 10;
        g.drawVerticalLine(x, static_cast<float>(content.getY()), static_cast<float>(content.getBottom()));
    }
    g.drawHorizontalLine(content.getY() + content.getHeight() / 4,
                         static_cast<float>(content.getX()), static_cast<float>(content.getRight()));
    g.drawHorizontalLine(content.getY() + content.getHeight() * 3 / 4,
                         static_cast<float>(content.getX()), static_cast<float>(content.getRight()));
    const auto duration = snapshot.sampleRate > 0.0
                        ? static_cast<double>(snapshot.frames) / snapshot.sampleRate : 0.0;
    if (snapshot.frames > 0)
    {
        g.setColour(mint);
        processor.drawWaveform(g, content, duration);
    }
    else
    {
        g.setColour(ink);
        g.setFont(uiFont(21.0f, juce::Font::bold));
        g.drawText(snapshot.state == capture::State::recording ? "Listening to your input..."
                                                               : (completedOnly ? "La muestra aparecera aqui al pulsar Stop." : "Lista para la siguiente toma."),
                   content, juce::Justification::centred);
    }
    g.setColour(muted);
    g.setFont(12.0f);
    g.drawText(snapshot.sampleRate > 0.0 ? juce::String(snapshot.sampleRate / 1000.0, 1) + " kHz"
                                        : "DAW input", footer, juce::Justification::centredLeft);
    g.drawText(formatTime(duration), footer, juce::Justification::centredRight);
    setMouseCursor(canDrag ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void SampleWaveform::mouseDown(const juce::MouseEvent& event)
{
    dragStarted = false;
    dragFile = juce::File {};
    if (event.mods.isLeftButtonDown() && snapshot.hasUsableFile && !isBusy(snapshot.state))
        dragFile = snapshot.file;
}

void SampleWaveform::mouseDrag(const juce::MouseEvent& event)
{
    if (dragStarted || !dragFile.existsAsFile() || event.getDistanceFromDragStart() < 6)
        return;
    // Native file drag, not startDragging(), which is for JUCE-internal data.
    dragStarted = true;
    const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles(
        juce::StringArray { dragFile.getFullPathName() }, false, this);
    if (!started)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SampleWaveform::mouseUp(const juce::MouseEvent&)
{
    dragStarted = false;
    dragFile = juce::File {};
}

LoopCaptureAudioProcessorEditor::LoopCaptureAudioProcessorEditor(LoopCaptureAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), waveform(p), samplePreview(p, true)
{
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);
    inputGain.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    inputGain.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 28);
    inputGain.setTextValueSuffix(" dB");
    inputGain.setDoubleClickReturnValue(true, 0.0);
    inputGain.setTooltip("Input gain. Captured audio and meters are measured before Monitor mute.");
    monitorButton.setClickingTogglesState(true);
    monitorButton.setTooltip("Listen to input. Turning this off does not mute the recording.");
    revealButton.setTooltip("Reveal the WAV. Copy it to your project folder to keep it permanently.");
    gainLabel.setText("INPUT GAIN", juce::dontSendNotification);
    gainLabel.setFont(uiFont(11.0f, juce::Font::bold));
    timeLabel.setFont(uiFont(25.0f, juce::Font::plain, true));
    timeLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setFont(uiFont(13.0f));
    hintLabel.setText("Route the virtual cable into this DAW track. Record, stop, then drag the sample.",
                      juce::dontSendNotification);
    hintLabel.setFont(uiFont(12.0f));
    for (juce::Component* component : std::array<juce::Component*, 14> { &inputGain,
          &recordButton, &monitorButton, &revealButton, &gainLabel, &timeLabel,
          &statusLabel, &hintLabel, &meter, &waveform, &samplePreview, &saveButton, &themeButton, &viewButton })
        addAndMakeVisible(component);

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "inputGain", inputGain);
    monitorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, "monitor", monitorButton);
    recordButton.onClick = [this]
    {
        exportMessage.clear();
        const auto state = processor.getSnapshot().state;
        if (state == capture::State::recording || state == capture::State::starting)
            processor.stopRecording();
        else if (state != capture::State::stopping)
            processor.startRecording();
        timerCallback();
    };
    revealButton.onClick = [this]
    {
        const auto snapshot = processor.getSnapshot();
        if (snapshot.hasUsableFile && !isBusy(snapshot.state))
            snapshot.file.revealToUser();
    };
    saveButton.onClick = [this] { saveSample(); };
    themeButton.onClick = [this] { lightMode = !lightMode; applyTheme(); };
    viewButton.onClick = [this]
    {
        mesaView = !mesaView;
        viewButton.setButtonText(mesaView ? "Interfaz: Mesa" : "Interfaz: Rack");
        inputGain.setSliderStyle(mesaView ? juce::Slider::LinearHorizontal
                                          : juce::Slider::RotaryHorizontalVerticalDrag);
        inputGain.setTextBoxStyle(mesaView ? juce::Slider::TextBoxRight : juce::Slider::TextBoxBelow,
                                  false, 100, 28);
        resized(); repaint();
    };
    applyTheme();
    setResizable(true, true);
    setResizeLimits(820, 720, 1400, 950);
    setSize(960, 720);
    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    timerCallback();
    startTimerHz(60);
}

LoopCaptureAudioProcessorEditor::~LoopCaptureAudioProcessorEditor()
{
    stopTimer();
    saveChooser.reset();
    monitorAttachment.reset();
    gainAttachment.reset();
    setLookAndFeel(nullptr);
    // Closing the editor does not stop a take. Processor/worker own its lifetime.
}

void LoopCaptureAudioProcessorEditor::timerCallback()
{
    const auto [background, panel, line, ink, muted, mint, coral] = palette(lightMode);
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto delta = static_cast<float>(juce::jlimit(0.001, 0.25, (now - lastTickMs) / 1000.0));
    lastTickMs = now;
    meter.update(processor.consumePeaks(), delta);
    const auto snapshot = processor.getSnapshot();
    auto live = snapshot;
    if (!isBusy(snapshot.state)) { live.frames = 0; live.hasUsableFile = false; }
    waveform.update(live);
    auto completed = snapshot;
    if (isBusy(snapshot.state)) { completed.frames = 0; completed.hasUsableFile = false; }
    samplePreview.update(completed);
    // Only meters need 60 FPS; waveform has its own 30 Hz dirty region.
    if ((++tick % 2) == 0)
    { waveform.repaint(); samplePreview.repaint(); }
    const auto seconds = snapshot.sampleRate > 0.0
                       ? static_cast<double>(snapshot.frames) / snapshot.sampleRate : 0.0;
    timeLabel.setText(formatTime(seconds), juce::dontSendNotification);
    statusLabel.setText(!exportMessage.isEmpty() ? exportMessage
                         : (snapshot.message.isEmpty() ? "Ready for a new take" : snapshot.message),
                        juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId,
                           snapshot.state == capture::State::error ? coral : muted);
    const auto recording = snapshot.state == capture::State::recording;
    const auto starting = snapshot.state == capture::State::starting;
    const auto stopping = snapshot.state == capture::State::stopping;
    recordButton.setButtonText(stopping ? "Finalising..." : (recording || starting ? "Stop" : "Record"));
    recordButton.setEnabled(!stopping);
    revealButton.setEnabled(snapshot.hasUsableFile && !isBusy(snapshot.state));
    saveButton.setEnabled(snapshot.hasUsableFile && !isBusy(snapshot.state) && !saving);
}

void CaptureLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float position, float startAngle, float endAngle, juce::Slider&)
{
    const auto [background, panel, line, ink, muted, mint, coral] = palette(lightMode);
    const auto radius = juce::jmin(static_cast<float>(width), static_cast<float>(height)) * 0.5f - 12.0f;
    const auto centre = juce::Point<float>(static_cast<float>(x) + static_cast<float>(width) * 0.5f,
                                          static_cast<float>(y) + static_cast<float>(height) * 0.5f);
    const auto bounds = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
    juce::DropShadow(juce::Colours::black.withAlpha(0.25f), 14, { 3, 5 }).drawForRectangle(g, bounds.toNearestInt());
    g.setGradientFill(juce::ColourGradient(panel.brighter(0.12f), bounds.getTopLeft(),
                                          background.darker(0.15f), bounds.getBottomRight(), false));
    g.fillEllipse(bounds); g.setColour(line); g.drawEllipse(bounds, 1.0f);
    juce::Path track, active;
    track.addCentredArc(centre.x, centre.y, radius - 6.0f, radius - 6.0f, 0.0f, startAngle, endAngle, true);
    const auto angle = startAngle + position * (endAngle - startAngle);
    active.addCentredArc(centre.x, centre.y, radius - 6.0f, radius - 6.0f, 0.0f, startAngle, angle, true);
    const juce::PathStrokeType stroke(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour(line); g.strokePath(track, stroke); g.setColour(mint); g.strokePath(active, stroke);
    const auto pointer = centre.getPointOnCircumference(radius * 0.65f, angle);
    g.drawLine(juce::Line<float>(centre.getPointOnCircumference(radius * 0.45f, angle), pointer), 4.0f);
    g.setFont(uiFont(12.0f, juce::Font::plain, true));g.setColour(muted);
    g.drawText("GAIN", bounds.toNearestInt().withTrimmedTop(static_cast<int>(radius)), juce::Justification::centred);
}

void LoopCaptureAudioProcessorEditor::applyTheme()
{
    const auto [background, panel, line, ink, muted, mint, coral] = palette(lightMode);
    lookAndFeel.setLightMode(lightMode);
    meter.setLightMode(lightMode); waveform.setLightMode(lightMode); samplePreview.setLightMode(lightMode);
    recordButton.setColour(juce::TextButton::buttonColourId, mint);
    recordButton.setColour(juce::TextButton::textColourOffId, background);
    for (auto* label : { &statusLabel, &hintLabel, &gainLabel })
        label->setColour(juce::Label::textColourId, muted);
    themeButton.setButtonText(lightMode ? "Modo oscuro" : "Modo claro");
    sendLookAndFeelChange(); repaint();
}

void LoopCaptureAudioProcessorEditor::saveSample()
{
    const auto snapshot = processor.getSnapshot();
    if (saving || !snapshot.hasUsableFile || isBusy(snapshot.state)) return;
    const auto sourceFile = snapshot.file; // Pin the completed take before opening the dialog.
    saveChooser = std::make_unique<juce::FileChooser>("Guardar muestra WAV",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(sourceFile.getFileName()), "*.wav");
    saving = true; saveButton.setEnabled(false);
    const juce::Component::SafePointer<LoopCaptureAudioProcessorEditor> safe(this);
    saveChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, sourceFile](const juce::FileChooser& chooser)
        {
            if (!safe) return;
            const auto destination = chooser.getResult();
            if (destination == juce::File {}) { safe->saving = false; safe->timerCallback(); return; }
            const auto started = juce::Thread::launch([safe, sourceFile, destination]
            {
                const auto ok = sourceFile == destination || sourceFile.copyFileTo(destination);
                juce::MessageManager::callAsync([safe, ok, destination]
                {
                    if (!safe) return;
                    safe->saving = false;
                    safe->exportMessage = ok ? "WAV guardado: " + destination.getFullPathName()
                                             : "No se pudo guardar el WAV. Revisa la carpeta de destino.";
                    safe->timerCallback();
                });
            });
            if (!started) { safe->saving = false; safe->exportMessage = "No se pudo iniciar el guardado."; safe->timerCallback(); }
        });
}

void LoopCaptureAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto [background, panel, line, ink, muted, mint, coral] = palette(lightMode);
    g.setGradientFill(juce::ColourGradient(panel, 0.0f, 0.0f, background,
                                          static_cast<float>(getWidth()), static_cast<float>(getHeight()), false));
    g.fillAll();
    g.setColour(ink);g.setFont(uiFont(28.0f));
    g.drawText("JUCE PLUGGIN", 28, 18, 330, 38, juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(uiFont(11.0f, juce::Font::plain, true));
    g.drawText(juce::String::fromUTF8("BY FR3DO - Demo en pañales"), 29, 57, 360, 20, juce::Justification::centredLeft);
    g.setColour(line);g.drawHorizontalLine(93, 28.0f, static_cast<float>(getWidth() - 28));
    g.setColour(muted);g.drawText("FR3DO / DEMO", getWidth() - 220, getHeight() - 25, 190, 18,
                                 juce::Justification::centredRight);
}

void LoopCaptureAudioProcessorEditor::resized()
{
    themeButton.setBounds(getWidth() - 150, 26, 122, 40);
    viewButton.setBounds(getWidth() - 296, 26, 136, 40);
    auto area = getLocalBounds().reduced(28);area.removeFromTop(80);area.removeFromBottom(24);
    auto transport = area.removeFromTop(46);
    recordButton.setBounds(transport.removeFromLeft(124));transport.removeFromLeft(12);
    monitorButton.setBounds(transport.removeFromLeft(124));
    timeLabel.setBounds(transport.removeFromRight(184));
    area.removeFromTop(16);
    auto footer = area.removeFromBottom(52);
    statusLabel.setBounds(footer.removeFromTop(26));hintLabel.setBounds(footer);
    area.removeFromBottom(12);
    auto exports = area.removeFromBottom(42);
    saveButton.setBounds(exports.removeFromRight(140));exports.removeFromRight(12);
    revealButton.setBounds(exports.removeFromRight(124));
    area.removeFromBottom(12);
    samplePreview.setBounds(area.removeFromBottom(174));area.removeFromBottom(18);
    if (mesaView)
    {
        auto row = area.removeFromBottom(48);gainLabel.setBounds(row.removeFromLeft(108));
        inputGain.setBounds(row);area.removeFromBottom(12);
        meter.setBounds(area.removeFromRight(125));area.removeFromRight(16);waveform.setBounds(area);
    }
    else
    {
        auto knob = area.removeFromLeft(180);gainLabel.setBounds(knob.removeFromTop(20));inputGain.setBounds(knob);
        area.removeFromLeft(18);meter.setBounds(area.removeFromRight(125));area.removeFromRight(16);waveform.setBounds(area);
    }
}
