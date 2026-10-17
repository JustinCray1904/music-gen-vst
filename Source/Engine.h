// ChordGen engine: genre-aware chords, bass and melody generation.
// Plain C++17, no JUCE dependency, so it can be unit-tested on its own.
// A direct port of the FL Studio Piano Roll script.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cg
{
constexpr int PPQ = 96;   // ticks per quarter note

// One entry per user-facing setting. The plugin builds its parameters and UI from this table.
enum ParamIndex
{
    pOutput, pGenre, pKey, pScale, pProgression, pChordType, pChordColor, pColorAmount,
    pOctave, pChordLength, pChordTiming, pChordVariation, pInversion, pStrum,
    pBass, pArpeggio, pArpRate, pArpOctaves,
    pSwing, pSwingGrid, pVelocity, pHumanize,
    pMelodyRhythm, pMelodySync, pMelodyFeel, pMelodyContour, pMelodyOctave, pLeaps, pTension,
    kParamCount
};

enum class Kind { Choice, Int, Bool };

struct ParamDesc
{
    const char* id;
    const char* label;
    const char* section;
    Kind kind;
    std::vector<std::string> options;   // Choice only
    int lo, hi, def;                    // Choice: 0..options-1
};

const std::vector<ParamDesc>& paramDescs();   // indexed by ParamIndex

using Settings = std::array<int, kParamCount>;
Settings defaultSettings();
Settings sanitize(Settings s);                // clamp everything into range

struct Texts
{
    std::string customScale = "0 2 3 5 7 8 11";
    std::string customProgression = "I V vi IV";
};

struct NoteEvent
{
    int note, tick, length, vel;
    int channel;          // 0 chords/arp, 1 bass, 2 melody
};

struct Pattern
{
    std::vector<NoteEvent> events;
    int loopTicks = PPQ * 16;
};

// Every result ever generated, so nothing repeats. Persistent when given a file path.
class History
{
public:
    explicit History(std::string filePath = {});
    void load();
    bool contains(uint64_t sig) const { return seen.count(sig) != 0; }
    void add(uint32_t settingsKey, uint64_t sig);
    void pruneOldestHalf(uint32_t settingsKey);
    void clear();
    size_t size() const { return entries.size(); }
    int repairedLines() const { return repaired; }

private:
    void rewrite();
    std::string path;
    std::vector<std::pair<uint32_t, uint64_t>> entries;
    std::unordered_set<uint64_t> seen;
    int repaired = 0;
};

struct GenerateResult
{
    Pattern pattern;
    bool fresh = true;                      // false only if even a history prune could not find something new
    std::vector<std::string> messages;      // warnings for the UI (ignored tokens, pruning, ...)
};

GenerateResult generate(const Settings& settings, const Texts& texts, History* history, uint64_t seed);

// Exposed for tests / UI
struct ScaleView { std::vector<int> notes, chords; bool minorFamily; };
ScaleView resolveScale(const Settings& s, const Texts& t, std::vector<std::string>* messages = nullptr);
uint64_t fingerprint(const std::vector<NoteEvent>& events);
}
