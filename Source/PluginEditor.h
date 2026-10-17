#pragma once

#include <map>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// ------------------------------------------------------------ dark, rounded look
class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr juce::uint32 bg = 0xff101218, card = 0xff191c25, edge = 0xff2a2f3e, accent = 0xff5865ff,
                                  teal = 0xff49d6c1, orange = 0xffffa95c, text = 0xffe8ebf4, muted = 0xff8d95aa;

    ModernLookAndFeel()
    {
        using namespace juce;
        setColour(ResizableWindow::backgroundColourId, Colour(bg));
        setColour(Label::textColourId, Colour(text));
        setColour(ComboBox::backgroundColourId, Colour(0xff222736));
        setColour(ComboBox::textColourId, Colour(text));
        setColour(ComboBox::outlineColourId, Colours::transparentBlack);
        setColour(PopupMenu::backgroundColourId, Colour(0xff1e2230));
        setColour(PopupMenu::textColourId, Colour(text));
        setColour(PopupMenu::highlightedBackgroundColourId, Colour(accent));
        setColour(PopupMenu::highlightedTextColourId, Colours::white);
        setColour(Slider::textBoxTextColourId, Colour(text));
        setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour(TextEditor::backgroundColourId, Colour(0xff222736));
        setColour(TextEditor::textColourId, Colour(text));
        setColour(TextEditor::outlineColourId, Colour(edge));
        setColour(TextEditor::focusedOutlineColourId, Colour(accent));
        setColour(TextButton::buttonColourId, Colour(0xff262b3b));
        setColour(TextButton::textColourOffId, Colour(text));
        setColour(TextButton::textColourOnId, Colour(text));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& colour, bool over, bool down) override
    {
        auto c = colour;
        if (down) c = c.darker(0.25f);
        else if (over) c = c.brighter(0.12f);
        g.setColour(c);
        g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(0.5f), 9.0f);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(0.5f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(r, 7.0f);
        juce::Path p;
        const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
        p.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour(juce::Colour(muted));
        g.fillPath(p);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour(juce::Colour(edge));
        g.fillRoundedRectangle((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
        g.setColour(juce::Colour(accent));
        g.fillRoundedRectangle((float) x, cy - 2.0f, juce::jmax(0.0f, pos - (float) x), 4.0f, 2.0f);
        g.setColour(juce::Colours::white);
        g.fillEllipse(pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        const auto r = b.getLocalBounds().toFloat();
        const juce::Rectangle<float> track(r.getX() + 2.0f, r.getCentreY() - 9.0f, 34.0f, 18.0f);
        const bool on = b.getToggleState();
        g.setColour(juce::Colour(on ? accent : edge));
        g.fillRoundedRectangle(track, 9.0f);
        g.setColour(juce::Colours::white);
        g.fillEllipse(on ? track.getRight() - 16.0f : track.getX() + 2.0f, track.getY() + 2.0f, 14.0f, 14.0f);
        if (b.getButtonText().isNotEmpty())
        {
            g.setColour(juce::Colour(text));
            g.setFont(14.0f);
            const int tx = (int) track.getRight() + 8;
            g.drawText(b.getButtonText(), tx, 0, b.getWidth() - tx, b.getHeight(), juce::Justification::centredLeft);
        }
    }
};

// ------------------------------------------------------------ piano-roll preview (one lane per part)
class PianoRollView : public juce::Component
{
public:
    explicit PianoRollView(ChordGenProcessor& p) : proc(p) {}
    void paint(juce::Graphics& g) override;

private:
    ChordGenProcessor& proc;
};

// ------------------------------------------------------------ drag the pattern out as a .mid file
class DragMidiPill : public juce::Component
{
public:
    explicit DragMidiPill(ChordGenProcessor& p) : proc(p) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }

private:
    ChordGenProcessor& proc;
    bool dragging = false;
};

// ------------------------------------------------------------ the whole interface, laid out at a fixed design size
class ChordGenContent : public juce::Component, private juce::Timer
{
public:
    static constexpr int designW = 1100;
    static constexpr int designH = 760;

    explicit ChordGenContent(ChordGenProcessor&);
    ~ChordGenContent() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using ComboAttach = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Row
    {
        juce::Label label;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<ComboAttach> comboAttach;     // attachments are declared after the control,
        std::unique_ptr<SliderAttach> sliderAttach;   // so they are destroyed first
        std::unique_ptr<ButtonAttach> buttonAttach;
    };

    void timerCallback() override;
    void layoutCards(juce::Rectangle<int> area);

    ChordGenProcessor& proc;

    juce::TextButton generateBtn { "Generate" }, playBtn { "Play" }, resetBtn { "Reset history" };
    juce::ToggleButton soundToggle { "Sound" };
    std::unique_ptr<ButtonAttach> soundAttach;
    DragMidiPill dragPill;
    PianoRollView roll;
    juce::Label statusLabel;

    std::vector<std::unique_ptr<Row>> rows;
    std::map<juce::String, std::vector<int>> sectionRows;   // section name -> indexes into rows
    juce::Label scaleLabel, progLabel;
    juce::TextEditor scaleText, progText;

    std::vector<std::pair<juce::String, juce::Rectangle<int>>> cards;
    uint32_t lastVersion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenContent)
};

// ------------------------------------------------------------ the plugin window: resizable, scales the content
class ChordGenEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChordGenEditor(ChordGenProcessor&);
    ~ChordGenEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ModernLookAndFeel laf;   // declared first, destroyed last
    ChordGenContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenEditor)
};
