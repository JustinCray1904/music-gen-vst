// A small built-in polyphonic synth so ChordGen makes sound on its own.
#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

struct SimpleSound : public juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class SimpleVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<SimpleSound*>(s) != nullptr; }

    void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        const double sr = getSampleRate();
        freq = juce::MidiMessage::getMidiNoteInHertz(midiNote);
        level = velocity * 0.16f;
        phase = 0.0;
        lp = 0.0f;
        const double cutoff = juce::jmin(freq * 5.0, 7000.0);
        coeff = (float) (1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * cutoff / sr));
        adsr.setSampleRate(sr);
        adsr.setParameters({ 0.008f, 0.30f, 0.55f, 0.40f });
        adsr.noteOn();
    }

    void stopNote(float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        if (!isVoiceActive()) return;
        const double inc = freq / getSampleRate();
        for (int i = 0; i < numSamples; ++i)
        {
            const float saw = 2.0f * (float) phase - 1.0f;
            const float sine = std::sin(2.0f * juce::MathConstants<float>::pi * (float) phase);
            const float raw = 0.6f * saw + 0.4f * sine;
            lp += coeff * (raw - lp);
            const float s = lp * adsr.getNextSample() * level;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.addSample(ch, startSample + i, s);
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
            if (!adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    juce::ADSR adsr;
    double freq = 440.0, phase = 0.0;
    float level = 0.1f, lp = 0.0f, coeff = 0.2f;
};
