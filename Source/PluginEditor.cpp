#include "PluginEditor.h"

#include <algorithm>

// ============================================================ piano roll preview
void PianoRollView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff151821));
    g.fillRoundedRectangle(bounds, 12.0f);

    const auto pat = proc.currentPattern();
    if (pat == nullptr || pat->events.empty())
    {
        g.setColour(juce::Colour(ModernLookAndFeel::muted));
        g.setFont(16.0f);
        g.drawText("Press Generate to create a pattern", getLocalBounds(), juce::Justification::centred);
        return;
    }

    int lo = 127, hi = 0;
    for (const auto& e : pat->events) { lo = std::min(lo, e.note); hi = std::max(hi, e.note); }
    lo -= 2;
    hi += 2;
    const int range = std::max(12, hi - lo + 1);

    const auto area = bounds.reduced(12.0f, 14.0f);
    const float pxPerTick = area.getWidth() / (float) pat->loopTicks;
    const float noteH = area.getHeight() / (float) range;

    for (int t = 0; t <= pat->loopTicks; t += cg::PPQ)
    {
        const bool bar = t % (cg::PPQ * 4) == 0;
        g.setColour(juce::Colour(bar ? 0xff343b52 : 0xff1f2433));
        g.drawVerticalLine((int) (area.getX() + (float) t * pxPerTick), area.getY(), area.getBottom());
    }
    for (int n = lo; n < lo + range; ++n)
        if (n % 12 == 0)
        {
            g.setColour(juce::Colour(0xff252b3c));
            g.drawHorizontalLine((int) (area.getBottom() - (float) (n - lo) * noteH), area.getX(), area.getRight());
        }

    const juce::Colour colours[3] = { juce::Colour(ModernLookAndFeel::accent), juce::Colour(ModernLookAndFeel::orange),
                                      juce::Colour(ModernLookAndFeel::teal) };
    for (const auto& e : pat->events)
    {
        const float x = area.getX() + (float) e.tick * pxPerTick;
        const float w = std::max(2.0f, (float) std::min(e.length, pat->loopTicks - e.tick) * pxPerTick - 1.0f);
        const float y = area.getBottom() - (float) (e.note - lo + 1) * noteH;
        g.setColour(colours[std::clamp(e.channel, 0, 2)].withAlpha(0.45f + (float) e.vel / 230.0f));
        g.fillRoundedRectangle(x, y + 1.0f, w, std::max(2.0f, noteH - 2.0f), 2.5f);
    }

    if (proc.uiRunning.load())
    {
        const float x = area.getX() + (float) (proc.uiBeat.load() * cg::PPQ) * pxPerTick;
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawVerticalLine((int) x, area.getY(), area.getBottom());
    }

    // legend
    const char* names[3] = { "Chords", "Bass", "Melody" };
    float lx = bounds.getRight() - 14.0f;
    g.setFont(12.0f);
    for (int i = 2; i >= 0; --i)
    {
        lx -= 62.0f;
        g.setColour(colours[i]);
        g.fillEllipse(lx, 7.0f, 8.0f, 8.0f);
        g.setColour(juce::Colour(ModernLookAndFeel::muted));
        g.drawText(names[i], (int) lx + 12, 3, 50, 16, juce::Justification::centredLeft);
    }
}

// ============================================================ drag pill
void DragMidiPill::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff262b3b));
    g.fillRoundedRectangle(r, 9.0f);
    g.setColour(juce::Colour(ModernLookAndFeel::accent));
    g.drawRoundedRectangle(r, 9.0f, 1.2f);
    g.setColour(juce::Colour(ModernLookAndFeel::text));
    g.setFont(14.0f);
    g.drawText("Drag MIDI", getLocalBounds(), juce::Justification::centred);
}

void DragMidiPill::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    const auto file = proc.exportMidiFile();
    if (!file.existsAsFile()) return;
    dragging = true;
    juce::StringArray files;
    files.add(file.getFullPathName());
    juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this);
    dragging = false;
}

// ============================================================ editor
ChordGenEditor::ChordGenEditor(ChordGenProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p), dragPill(p), roll(p)
{
    setLookAndFeel(&laf);
    setSize(1100, 720);

    // ---- top bar
    generateBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(ModernLookAndFeel::accent));
    generateBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    generateBtn.onClick = [this] { proc.generate(); roll.repaint(); };
    addAndMakeVisible(generateBtn);

    playBtn.onClick = [this] { if (proc.previewPlaying.load()) proc.stopPreview(); else proc.startPreview(); };
    addAndMakeVisible(playBtn);

    resetBtn.onClick = [this] { proc.resetHistory(); };
    addAndMakeVisible(resetBtn);

    addAndMakeVisible(soundToggle);
    soundAttach = std::make_unique<ButtonAttach>(proc.apvts, "sound", soundToggle);

    addAndMakeVisible(dragPill);
    addAndMakeVisible(roll);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(ModernLookAndFeel::muted));
    statusLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);

    // ---- one control per engine parameter, grouped by section
    const auto& descs = cg::paramDescs();
    for (size_t i = 0; i < descs.size(); ++i)
    {
        const auto& d = descs[i];
        auto row = std::make_unique<Row>();
        row->label.setText(d.label, juce::dontSendNotification);
        row->label.setColour(juce::Label::textColourId, juce::Colour(ModernLookAndFeel::muted));
        addAndMakeVisible(row->label);

        switch (d.kind)
        {
            case cg::Kind::Choice:
            {
                auto box = std::make_unique<juce::ComboBox>();
                juce::StringArray opts;
                for (const auto& o : d.options) opts.add(o);
                box->addItemList(opts, 1);
                row->comboAttach = std::make_unique<ComboAttach>(proc.apvts, d.id, *box);
                row->control = std::move(box);
                break;
            }
            case cg::Kind::Int:
            {
                auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
                slider->setNumDecimalPlacesToDisplay(0);
                row->sliderAttach = std::make_unique<SliderAttach>(proc.apvts, d.id, *slider);
                row->control = std::move(slider);
                break;
            }
            case cg::Kind::Bool:
            {
                auto toggle = std::make_unique<juce::ToggleButton>();
                row->buttonAttach = std::make_unique<ButtonAttach>(proc.apvts, d.id, *toggle);
                row->control = std::move(toggle);
                break;
            }
        }
        addAndMakeVisible(*row->control);
        sectionRows[d.section].push_back((int) rows.size());
        rows.push_back(std::move(row));
    }

    // ---- custom text boxes
    scaleLabel.setText("Custom scale", juce::dontSendNotification);
    progLabel.setText("Custom chords", juce::dontSendNotification);
    for (auto* l : { &scaleLabel, &progLabel })
    {
        l->setColour(juce::Label::textColourId, juce::Colour(ModernLookAndFeel::muted));
        addAndMakeVisible(*l);
    }
    scaleText.setText(proc.getText("customScale", "0 2 3 5 7 8 11"), juce::dontSendNotification);
    progText.setText(proc.getText("customProgression", "I V vi IV"), juce::dontSendNotification);
    scaleText.setTooltip("Used when Scale is 'Custom (text)'. Semitones (0 2 3 5 7 8 11) or note names (C D Eb F G Ab B).");
    progText.setTooltip("Used when Progression is 'Custom (text)'. Roman numerals (ii7 V7 Imaj7), degrees (1 5 6 4), beats (I:2 V:2).");
    scaleText.onTextChange = [this] { proc.setText("customScale", scaleText.getText()); };
    progText.onTextChange = [this] { proc.setText("customProgression", progText.getText()); };
    addAndMakeVisible(scaleText);
    addAndMakeVisible(progText);

    lastVersion = proc.patternVersion.load();
    startTimerHz(30);
}

ChordGenEditor::~ChordGenEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void ChordGenEditor::timerCallback()
{
    const auto v = proc.patternVersion.load();
    if (v != lastVersion) { lastVersion = v; roll.repaint(); }
    else if (proc.uiRunning.load()) roll.repaint();

    const auto s = proc.getStatus();
    if (s != statusLabel.getText()) statusLabel.setText(s, juce::dontSendNotification);

    const juce::String want = proc.previewPlaying.load() ? "Stop" : "Play";
    if (playBtn.getButtonText() != want) playBtn.setButtonText(want);
}

void ChordGenEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(ModernLookAndFeel::bg));

    g.setColour(juce::Colour(ModernLookAndFeel::text));
    g.setFont(26.0f);
    g.drawText("ChordGen", 16, 16, 150, 48, juce::Justification::centredLeft);

    for (const auto& c : cards)
    {
        g.setColour(juce::Colour(ModernLookAndFeel::card));
        g.fillRoundedRectangle(c.second.toFloat(), 14.0f);
        g.setColour(juce::Colour(ModernLookAndFeel::edge));
        g.drawRoundedRectangle(c.second.toFloat().reduced(0.5f), 14.0f, 1.0f);
        g.setColour(juce::Colour(ModernLookAndFeel::accent));
        g.setFont(13.0f);
        g.drawText(c.first.toUpperCase(), c.second.getX() + 14, c.second.getY() + 8, c.second.getWidth() - 28, 20,
                   juce::Justification::centredLeft);
    }
}

void ChordGenEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    auto top = area.removeFromTop(48);
    top.removeFromLeft(150);   // title is painted
    generateBtn.setBounds(top.removeFromLeft(120).reduced(0, 5));
    top.removeFromLeft(10);
    playBtn.setBounds(top.removeFromLeft(80).reduced(0, 5));
    top.removeFromLeft(10);
    soundToggle.setBounds(top.removeFromLeft(90).reduced(0, 10));
    top.removeFromLeft(6);
    dragPill.setBounds(top.removeFromLeft(130).reduced(0, 5));
    top.removeFromLeft(10);
    resetBtn.setBounds(top.removeFromLeft(120).reduced(0, 5));
    top.removeFromLeft(12);
    statusLabel.setBounds(top);

    area.removeFromTop(10);
    roll.setBounds(area.removeFromTop(200));
    area.removeFromTop(12);
    layoutCards(area);
}

void ChordGenEditor::layoutCards(juce::Rectangle<int> area)
{
    constexpr int headerH = 34, rowH = 28, gap = 12;
    cards.clear();

    const int colW = (area.getWidth() - 2 * gap) / 3;
    const std::vector<std::vector<juce::String>> columns = { { "Song", "Bass & Arp" }, { "Chords" }, { "Groove", "Melody" } };

    auto placeRow = [](juce::Rectangle<int> r, juce::Component& label, juce::Component& control) {
        r = r.reduced(0, 2);
        label.setBounds(r.removeFromLeft((int) ((float) r.getWidth() * 0.38f)));
        control.setBounds(r.withSizeKeepingCentre(r.getWidth(), 24));
    };

    for (size_t c = 0; c < columns.size(); ++c)
    {
        int y = area.getY();
        const int x = area.getX() + (int) c * (colW + gap);
        for (const auto& name : columns[c])
        {
            const auto it = sectionRows.find(name);
            if (it == sectionRows.end()) continue;
            const int extra = (name == "Song" || name == "Chords") ? 1 : 0;
            const int h = headerH + ((int) it->second.size() + extra) * rowH + 12;
            const juce::Rectangle<int> card(x, y, colW, h);
            cards.push_back({ name, card });

            auto inner = card.reduced(14, 0);
            inner.removeFromTop(headerH);
            for (int ri : it->second) placeRow(inner.removeFromTop(rowH), rows[(size_t) ri]->label, *rows[(size_t) ri]->control);
            if (name == "Song") placeRow(inner.removeFromTop(rowH), scaleLabel, scaleText);
            if (name == "Chords") placeRow(inner.removeFromTop(rowH), progLabel, progText);
            y += h + gap;
        }
    }
    repaint();
}
