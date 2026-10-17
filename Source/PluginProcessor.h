#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine.h"
#include "SimpleSynth.h"

class ChordGenProcessor : public juce::AudioProcessor
{
public:
    ChordGenProcessor();
    ~ChordGenProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ChordGen"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread) ----
    void generate();
    void resetHistory();
    std::shared_ptr<const cg::Pattern> currentPattern() const;
    juce::File exportMidiFile() const;
    juce::String getStatus() const { return status; }
    juce::String getText(const juce::String& name, const juce::String& fallback) const;
    void setText(const juce::String& name, const juce::String& value);

    void startPreview() { restartPreview = true; previewPlaying = true; }
    void stopPreview() { previewPlaying = false; }

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<bool> previewPlaying { false };
    std::atomic<bool> uiRunning { false };
    std::atomic<double> uiBeat { 0.0 };           // position inside the loop, in beats
    std::atomic<uint32_t> patternVersion { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    cg::Settings currentSettings() const;
    void setPattern(std::shared_ptr<const cg::Pattern> p);
    static juce::String serialize(const cg::Pattern& p);
    static std::shared_ptr<const cg::Pattern> deserialize(const juce::String& s);
    static void allNotesOff(juce::MidiBuffer& out, int sample);

    mutable juce::SpinLock patternLock;
    std::shared_ptr<const cg::Pattern> pattern;

    juce::Synthesiser synth;
    juce::String status;

    // audio-thread state
    bool wasRunning = false;
    double expectedBeat = 0.0, internalBeat = 0.0;
    std::atomic<bool> needReset { false };
    std::atomic<bool> restartPreview { false };
    std::atomic<double> lastBpm { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenProcessor)
};
