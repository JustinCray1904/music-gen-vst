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

    const juce::Colour colours[3] = { juce::Colour(ModernLookAndFeel::accent).brighter(0.35f), juce::Colour(ModernLookAndFeel::orange),
                                      juce::Colour(ModernLookAndFeel::teal) };

    // One lane per part, top to bottom: melody, chords, bass. Each lane has its own pitch range,
    // so every part is easy to read even when they sit in very different registers.
    struct Lane { int channel; float weight; int lo = 127, hi = 0; bool any = false; float y = 0, h = 0; };
    Lane lanes[3] = { { 2, 1.0f }, { 0, 1.5f }, { 1, 0.8f } };
    for (const auto& e : pat->events)
        for (auto& l : lanes)
            if (l.channel == e.channel) { l.any = true; l.lo = std::min(l.lo, e.note); l.hi = std::max(l.hi, e.note); }

    const auto area = bounds.reduced(14.0f, 16.0f);
    constexpr float laneGap = 8.0f;
    float totalWeight = 0.0f;
    int laneCount = 0;
    for (const auto& l : lanes) if (l.any) { totalWeight += l.weight; ++laneCount; }
    if (laneCount == 0) return;

    const float usable = area.getHeight() - laneGap * (float) (laneCount - 1);
    float y = area.getY();
    for (auto& l : lanes)
    {
        if (!l.any) continue;
        l.y = y;
        l.h = usable * l.weight / totalWeight;
        y += l.h + laneGap;
    }

    const float pxPerTick = area.getWidth() / (float) pat->loopTicks;

    // lane backgrounds
    for (const auto& l : lanes)
        if (l.any)
        {
            g.setColour(juce::Colour(0xff1a1e29));
            g.fillRoundedRectangle(area.getX(), l.y, area.getWidth(), l.h, 6.0f);
        }

    // beat / bar grid across all lanes
    for (int t = 0; t <= pat->loopTicks; t += cg::PPQ)
    {
        const bool bar = t % (cg::PPQ * 4) == 0;
        g.setColour(juce::Colour(bar ? 0xff3a4260 : 0xff232a3b));
        g.drawVerticalLine((int) (area.getX() + (float) t * pxPerTick), area.getY(), area.getBottom());
    }

    // notes
    for (const auto& e : pat->events)
    {
        const Lane* lane = nullptr;
        for (const auto& l : lanes) if (l.any && l.channel == e.channel) lane = &l;
        if (lane == nullptr) continue;

        const int lo = lane->lo - 1;
        const int range = std::max(10, lane->hi - lane->lo + 3);
        const float noteH = lane->h / (float) range;
        const float x = area.getX() + (float) e.tick * pxPerTick;
        const float w = std::max(3.0f, (float) std::min(e.length, pat->loopTicks - e.tick) * pxPerTick - 1.5f);
        const float ny = lane->y + lane->h - (float) (e.note - lo + 1) * noteH;
        g.setColour(colours[std::clamp(e.channel, 0, 2)].withAlpha(0.55f + (float) e.vel / 280.0f));
        g.fillRoundedRectangle(x, ny, w, std::max(4.0f, noteH - 1.0f), 2.5f);
    }

    if (proc.uiRunning.load())
    {
        const float x = area.getX() + (float) (proc.uiBeat.load() * cg::PPQ) * pxPerTick;
        g.setColour(juce::Colours::white.withAlpha(0.85f));
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
        g.fillEllipse(lx, 3.0f, 8.0f, 8.0f);
        g.setColour(juce::Colour(ModernLookAndFeel::muted));
        g.drawText(names[i], (int) lx + 12, -1, 50, 16, juce::Justification::centredLeft);
    }
}

// ============================================================ drag pill
void DragMidiPill::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff262b3b));
    g.fillRoundedRectangle(r, 9.0f);
    g.setColour(juce::Colour(ModernLookAndFeel::accent).brighter(0.3f));
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

// ============================================================ content (fixed design size)
ChordGenContent::ChordGenContent(ChordGenProcessor& p) : proc(p), dragPill(p), roll(p)
{
    // ---- top bar
    generateBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(ModernLookAndFeel::accent));
    generateBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    generateBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
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

    // Size is set LAST, after every child exists, so resized() lays all of them out.
    setSize(designW, designH);
    startTimerHz(30);
}

void ChordGenContent::timerCallback()
{
    const auto v = proc.patternVersion.load();
    if (v != lastVersion) { lastVersion = v; roll.repaint(); }
    else if (proc.uiRunning.load()) roll.repaint();

    const auto s = proc.getStatus();
    if (s != statusLabel.getText()) statusLabel.setText(s, juce::dontSendNotification);

    const juce::String want = proc.previewPlaying.load() ? "Stop" : "Play";
    if (playBtn.getButtonText() != want) playBtn.setButtonText(want);
}

void ChordGenContent::paint(juce::Graphics& g)
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
        g.setColour(juce::Colour(ModernLookAndFeel::accent).brighter(0.45f));
        g.setFont(13.0f);
        g.drawText(c.first.toUpperCase(), c.second.getX() + 14, c.second.getY() + 8, c.second.getWidth() - 28, 20,
                   juce::Justification::centredLeft);
    }
}

void ChordGenContent::resized()
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
    roll.setBounds(area.removeFromTop(230));
    area.removeFromTop(12);
    layoutCards(area);
}

void ChordGenContent::layoutCards(juce::Rectangle<int> area)
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

// ============================================================ editor window (resizable)
ChordGenEditor::ChordGenEditor(ChordGenProcessor& p) : juce::AudioProcessorEditor(&p), content(p)
{
    setLookAndFeel(&laf);
    addAndMakeVisible(content);

    setResizable(true, true);
    setResizeLimits(660, 432, 1650, 1080);
    getConstrainer()->setFixedAspectRatio((double) ChordGenContent::designW / (double) ChordGenContent::designH);
    setSize(880, 608);   // 80% of the design size, fits small laptop screens; drag the corner to enlarge
}

ChordGenEditor::~ChordGenEditor()
{
    setLookAndFeel(nullptr);
}

void ChordGenEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(ModernLookAndFeel::bg));
}

void ChordGenEditor::resized()
{
    // The content is always laid out at its design size, then scaled to fit the window.
    content.setBounds(0, 0, ChordGenContent::designW, ChordGenContent::designH);
    content.setTransform(juce::AffineTransform::scale((float) getWidth() / (float) ChordGenContent::designW));
}
