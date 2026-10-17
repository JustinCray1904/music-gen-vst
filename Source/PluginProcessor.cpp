#include "PluginProcessor.h"

#include <cmath>
#include <random>

#include "PluginEditor.h"

namespace
{
// One history shared by every ChordGen instance in the process, saved in the user's app-data folder.
class SharedHistory
{
public:
    static SharedHistory& get()
    {
        static SharedHistory instance;
        return instance;
    }
    std::mutex mutex;
    cg::History history;

private:
    SharedHistory() : history(makePath()) { history.load(); }
    static std::string makePath()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ChordGen");
        dir.createDirectory();
        return dir.getChildFile("history.txt").getFullPathName().toStdString();
    }
};
} // namespace

ChordGenProcessor::ChordGenProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ChordGenState", createLayout())
{
    for (int i = 0; i < 16; ++i) synth.addVoice(new SimpleVoice());
    synth.addSound(new SimpleSound());
}

juce::AudioProcessorValueTreeState::ParameterLayout ChordGenProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : cg::paramDescs())
    {
        const juce::String name = juce::String(d.section) + " " + d.label;
        switch (d.kind)
        {
            case cg::Kind::Choice:
            {
                juce::StringArray opts;
                for (const auto& o : d.options) opts.add(o);
                layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { d.id, 1 }, name, opts, d.def));
                break;
            }
            case cg::Kind::Int:
                layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { d.id, 1 }, name, d.lo, d.hi, d.def));
                break;
            case cg::Kind::Bool:
                layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { d.id, 1 }, name, d.def != 0));
                break;
        }
    }
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "sound", 1 }, "Built-in sound", true));
    return layout;
}

cg::Settings ChordGenProcessor::currentSettings() const
{
    cg::Settings s {};
    const auto& d = cg::paramDescs();
    for (int i = 0; i < cg::kParamCount; ++i)
    {
        auto* p = apvts.getRawParameterValue(d[(size_t) i].id);
        s[(size_t) i] = p != nullptr ? (int) std::lround(p->load()) : d[(size_t) i].def;
    }
    return s;
}

juce::String ChordGenProcessor::getText(const juce::String& name, const juce::String& fallback) const
{
    return apvts.state.getProperty(name, fallback).toString();
}

void ChordGenProcessor::setText(const juce::String& name, const juce::String& value)
{
    apvts.state.setProperty(name, value, nullptr);
}

// ------------------------------------------------------------ generating
void ChordGenProcessor::generate()
{
    cg::Texts t;
    t.customScale = getText("customScale", "0 2 3 5 7 8 11").toStdString();
    t.customProgression = getText("customProgression", "I V vi IV").toStdString();

    std::random_device rd;
    const uint64_t seed = ((uint64_t) rd() << 32) ^ (uint64_t) rd() ^ (uint64_t) juce::Time::getHighResolutionTicks();

    cg::GenerateResult r;
    {
        auto& shared = SharedHistory::get();
        std::lock_guard<std::mutex> lock(shared.mutex);
        r = cg::generate(currentSettings(), t, &shared.history, seed);
    }

    status.clear();
    for (const auto& m : r.messages) status += juce::String(m) + "  ";
    setPattern(std::make_shared<const cg::Pattern>(std::move(r.pattern)));
}

void ChordGenProcessor::resetHistory()
{
    auto& shared = SharedHistory::get();
    std::lock_guard<std::mutex> lock(shared.mutex);
    shared.history.clear();
    status = "History cleared.";
}

void ChordGenProcessor::setPattern(std::shared_ptr<const cg::Pattern> p)
{
    if (p != nullptr) setText("pattern", serialize(*p));
    {
        const juce::SpinLock::ScopedLockType lock(patternLock);
        pattern = std::move(p);
    }
    needReset = true;
    ++patternVersion;
}

std::shared_ptr<const cg::Pattern> ChordGenProcessor::currentPattern() const
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    return pattern;
}

juce::String ChordGenProcessor::serialize(const cg::Pattern& p)
{
    juce::String s(p.loopTicks);
    for (const auto& e : p.events)
        s << ";" << e.tick << "," << e.length << "," << e.note << "," << e.vel << "," << e.channel;
    return s;
}

std::shared_ptr<const cg::Pattern> ChordGenProcessor::deserialize(const juce::String& s)
{
    if (s.isEmpty()) return nullptr;
    juce::StringArray parts;
    parts.addTokens(s, ";", "");
    if (parts.size() < 1) return nullptr;
    auto p = std::make_shared<cg::Pattern>();
    p->loopTicks = juce::jmax(cg::PPQ, parts[0].getIntValue());
    for (int i = 1; i < parts.size(); ++i)
    {
        juce::StringArray f;
        f.addTokens(parts[i], ",", "");
        if (f.size() != 5) continue;
        cg::NoteEvent e { f[2].getIntValue(), f[0].getIntValue(), f[1].getIntValue(), f[3].getIntValue(), f[4].getIntValue() };
        if (e.note < 0 || e.note > 127 || e.tick < 0 || e.length < 1 || e.vel < 1 || e.vel > 127 || e.channel < 0 || e.channel > 2) continue;
        p->events.push_back(e);
    }
    return p;
}

// ------------------------------------------------------------ MIDI file for dragging into the DAW
juce::File ChordGenProcessor::exportMidiFile() const
{
    auto p = currentPattern();
    if (p == nullptr || p->events.empty()) return {};

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(cg::PPQ);

    juce::MidiMessageSequence conductor;
    const double bpm = juce::jlimit(30.0, 300.0, lastBpm.load());
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent((int) (60000000.0 / bpm)), 0.0);
    conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    conductor.addEvent(juce::MidiMessage::endOfTrack(), (double) p->loopTicks);
    mf.addTrack(conductor);

    static const char* names[3] = { "Chords", "Bass", "Melody" };
    for (int ch = 0; ch < 3; ++ch)
    {
        juce::MidiMessageSequence seq;
        bool any = false;
        for (const auto& e : p->events)
        {
            if (e.channel != ch) continue;
            const int end = juce::jmin(e.tick + e.length, p->loopTicks);
            if (end <= e.tick) continue;
            seq.addEvent(juce::MidiMessage::noteOn(ch + 1, e.note, (juce::uint8) e.vel), (double) e.tick);
            seq.addEvent(juce::MidiMessage::noteOff(ch + 1, e.note), (double) end);
            any = true;
        }
        if (!any) continue;
        seq.addEvent(juce::MidiMessage::textMetaEvent(3, names[ch]), 0.0);
        seq.addEvent(juce::MidiMessage::endOfTrack(), (double) p->loopTicks);
        seq.updateMatchedPairs();
        mf.addTrack(seq);
    }

    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("ChordGen " + juce::String(juce::Time::currentTimeMillis() % 1000000) + ".mid");
    file.deleteFile();
    if (auto stream = file.createOutputStream())
    {
        mf.writeTo(*stream);
        stream->flush();
    }
    return file;
}

// ------------------------------------------------------------ state
void ChordGenProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void ChordGenProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            auto p = deserialize(getText("pattern", {}));
            if (p != nullptr) setPattern(p);
        }
}

// ------------------------------------------------------------ audio
void ChordGenProcessor::prepareToPlay(double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
    wasRunning = false;
    needReset = true;
}

bool ChordGenProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ChordGenProcessor::allNotesOff(juce::MidiBuffer& out, int sample)
{
    for (int ch = 1; ch <= 3; ++ch) out.addEvent(juce::MidiMessage::allNotesOff(ch), sample);
}

void ChordGenProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midi.clear();   // ChordGen generates its own notes; incoming MIDI is ignored

    const int n = buffer.getNumSamples();
    const auto pat = currentPattern();

    double bpm = 120.0, hostPpq = 0.0;
    bool hostPlaying = false, havePpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) { hostPpq = *q; havePpq = true; }
        }
    if (bpm > 1.0) lastBpm = bpm;

    if (restartPreview.exchange(false)) internalBeat = 0.0;

    bool running = false, usingHost = false;
    double startBeat = 0.0;
    if (hostPlaying && havePpq) { running = true; usingHost = true; startBeat = hostPpq; }
    else if (previewPlaying.load()) { running = true; startBeat = internalBeat; }

    juce::MidiBuffer out;
    const bool reset = needReset.exchange(false);

    if (!running || pat == nullptr || pat->events.empty() || startBeat < 0.0 || bpm <= 1.0)
    {
        if (wasRunning || reset) allNotesOff(out, 0);
        wasRunning = false;
        uiRunning = false;
    }
    else
    {
        const double beatsPerSample = bpm / 60.0 / getSampleRate();
        const double endBeat = startBeat + n * beatsPerSample;
        const double loopBeats = pat->loopTicks / (double) cg::PPQ;

        // transport jumped (seek, loop, regenerated pattern): release anything still sounding
        if (!wasRunning || reset || std::abs(startBeat - expectedBeat) > 0.05) allNotesOff(out, 0);

        const long firstLoop = (long) std::floor(startBeat / loopBeats);
        const long lastLoop = (long) std::floor(endBeat / loopBeats);
        auto sampleAt = [&](double beat) { return juce::jlimit(0, juce::jmax(0, n - 1), (int) std::floor((beat - startBeat) / beatsPerSample)); };

        for (long loop = firstLoop; loop <= lastLoop; ++loop)      // note-offs first, so a retrigger never gets cut
            for (const auto& e : pat->events)
            {
                const double offB = loop * loopBeats + juce::jmin(e.tick + e.length, pat->loopTicks) / (double) cg::PPQ;
                if (offB >= startBeat && offB < endBeat)
                    out.addEvent(juce::MidiMessage::noteOff(e.channel + 1, e.note), sampleAt(offB));
            }
        for (long loop = firstLoop; loop <= lastLoop; ++loop)
            for (const auto& e : pat->events)
            {
                if (e.tick >= pat->loopTicks) continue;
                const double onB = loop * loopBeats + e.tick / (double) cg::PPQ;
                if (onB >= startBeat && onB < endBeat)
                    out.addEvent(juce::MidiMessage::noteOn(e.channel + 1, e.note, (juce::uint8) e.vel), sampleAt(onB));
            }

        expectedBeat = endBeat;
        wasRunning = true;
        if (!usingHost) internalBeat = endBeat;
        uiBeat = std::fmod(endBeat, loopBeats);
        uiRunning = true;
    }

    if (auto* soundParam = apvts.getRawParameterValue("sound"))
        if (soundParam->load() > 0.5f) synth.renderNextBlock(buffer, out, 0, n);

    midi.swapWith(out);
}

juce::AudioProcessorEditor* ChordGenProcessor::createEditor() { return new ChordGenEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChordGenProcessor(); }
