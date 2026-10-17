#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>

namespace cg
{
// ============================================================ parameters
static std::vector<std::string> kNoteNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }

static const std::vector<std::string> kScaleLabels = {
    "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Harmonic Minor", "Melodic Minor",
    "Major Pentatonic", "Minor Pentatonic", "Whole Tone", "Custom (text)" };

static const std::vector<std::string> kGenreNames = {
    "Pop", "Hip-Hop / Trap", "Lo-fi", "R&B", "EDM / House", "Jazz", "Rock", "Cinematic", "Afrobeats" };

static const std::vector<std::string> kFixedProgNames = {
    "I - V - vi - IV (pop)", "vi - IV - I - V (emo)", "I - vi - IV - V (50s)", "ii - V - I - I (jazz)",
    "i - VI - III - VII", "i - iv - v - i", "I - IV - V - IV" };
static const std::vector<std::vector<int>> kFixedProgs = {
    { 0, 4, 5, 3 }, { 5, 3, 0, 4 }, { 0, 5, 3, 4 }, { 1, 4, 0, 0 }, { 0, 5, 2, 6 }, { 0, 3, 4, 0 }, { 0, 3, 4, 3 } };

const std::vector<ParamDesc>& paramDescs()
{
    static const std::vector<ParamDesc> d = [] {
        std::vector<std::string> progs { "Auto (genre)", "Custom (text)" };
        progs.insert(progs.end(), kFixedProgNames.begin(), kFixedProgNames.end());
        const std::vector<std::string> off_on { "Off", "On" };
        std::vector<ParamDesc> v(kParamCount);
        auto choice = [&](int i, const char* id, const char* label, const char* section,
                          std::vector<std::string> opts, int def) {
            v[i] = { id, label, section, Kind::Choice, opts, 0, (int) opts.size() - 1, def };
        };
        auto integer = [&](int i, const char* id, const char* label, const char* section, int lo, int hi, int def) {
            v[i] = { id, label, section, Kind::Int, {}, lo, hi, def };
        };
        choice(pOutput, "output", "Output", "Song", { "Chords + Melody", "Chords only", "Melody only", "Scale preview" }, 0);
        choice(pGenre, "genre", "Genre", "Song", kGenreNames, 0);
        choice(pKey, "key", "Key", "Song", kNoteNames(), 0);
        choice(pScale, "scale", "Scale", "Song", kScaleLabels, 0);
        choice(pProgression, "progression", "Progression", "Chords", progs, 0);
        choice(pChordType, "chordType", "Type", "Chords", { "Auto (genre)", "Triad", "7th", "9th" }, 0);
        choice(pChordColor, "chordColor", "Color", "Chords", { "Off", "Borrowed", "Secondary dominants", "Both" }, 0);
        integer(pColorAmount, "colorAmount", "Color amount %", "Chords", 0, 100, 35);
        integer(pOctave, "octave", "Octave", "Chords", 1, 7, 4);
        integer(pChordLength, "chordLength", "Length (beats)", "Chords", 1, 8, 4);
        choice(pChordTiming, "chordTiming", "Timing", "Chords", { "Uniform", "Varied lengths", "Syncopated", "Varied + Syncopated" }, 0);
        choice(pChordVariation, "chordVariation", "Variation", "Chords", off_on, 1);
        integer(pInversion, "inversion", "Inversion", "Chords", 0, 3, 0);
        integer(pStrum, "strum", "Strum (ticks)", "Chords", 0, 60, 0);
        choice(pBass, "bass", "Bass", "Bass & Arp", { "Auto (genre)", "Off", "Root", "Smooth" }, 0);
        choice(pArpeggio, "arp", "Arp pattern", "Bass & Arp", { "Off", "Up", "Down", "Up-Down", "Random" }, 0);
        choice(pArpRate, "arpRate", "Arp rate", "Bass & Arp", { "1/4", "1/8", "1/16", "1/32" }, 2);
        integer(pArpOctaves, "arpOctaves", "Arp octaves", "Bass & Arp", 1, 3, 1);
        integer(pSwing, "swing", "Swing %", "Groove", 0, 100, 0);
        choice(pSwingGrid, "swingGrid", "Swing grid", "Groove", { "8th", "16th" }, 0);
        integer(pVelocity, "velocity", "Velocity", "Groove", 1, 127, 80);
        integer(pHumanize, "humanize", "Humanize", "Groove", 0, 40, 10);
        choice(pMelodyRhythm, "melodyRhythm", "Rhythm", "Melody", { "Auto (genre)", "Quarter", "Eighth", "Mixed", "Phrased" }, 0);
        choice(pMelodySync, "melodySync", "Syncopation", "Melody", { "Auto (genre)", "None", "Light", "Medium", "Heavy" }, 0);
        choice(pMelodyFeel, "melodyFeel", "Feel", "Melody", { "Auto (genre)", "Smooth", "Balanced", "Adventurous", "Sparse" }, 0);
        choice(pMelodyContour, "melodyContour", "Contour", "Melody", { "Auto (genre)", "Free", "Ascending", "Descending", "Arch", "Wave" }, 0);
        integer(pMelodyOctave, "melodyOctave", "Octave", "Melody", 2, 8, 5);
        integer(pLeaps, "leaps", "Leaps %", "Melody", 0, 60, 10);
        v[pTension] = { "tension", "Allow tension", "Melody", Kind::Bool, {}, 0, 1, 0 };
        return v;
    }();
    return d;
}

Settings defaultSettings()
{
    Settings s {};
    const auto& d = paramDescs();
    for (int i = 0; i < kParamCount; ++i) s[i] = d[i].def;
    return s;
}

Settings sanitize(Settings s)
{
    const auto& d = paramDescs();
    for (int i = 0; i < kParamCount; ++i) s[i] = std::max(d[i].lo, std::min(d[i].hi, s[i]));
    return s;
}

// ============================================================ music tables
namespace
{
using IntVec = std::vector<int>;
using Pat = std::vector<std::pair<double, double>>;

struct ScaleDef
{
    IntVec notes, chords;
    bool minor = false;
    std::vector<IntVec> sig;
};

ScaleDef sdef(IntVec notes, bool minor, IntVec chords = {}, std::vector<IntVec> sig = {})
{
    ScaleDef s;
    s.notes = notes;
    s.chords = chords.empty() ? notes : chords;
    s.minor = minor;
    s.sig = sig;
    return s;
}

const std::vector<ScaleDef>& builtinScales()
{
    static const std::vector<ScaleDef> v = {
        sdef({ 0, 2, 4, 5, 7, 9, 11 }, false),
        sdef({ 0, 2, 3, 5, 7, 8, 10 }, true),
        sdef({ 0, 2, 3, 5, 7, 9, 10 }, true, {}, { { 0, 3, 0, 3 }, { 0, 3, 6, 3 }, { 0, 6, 3, 0 }, { 0, 1, 3, 0 } }),
        sdef({ 0, 1, 3, 5, 7, 8, 10 }, true, {}, { { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 5, 1, 0 } }),
        sdef({ 0, 2, 4, 6, 7, 9, 11 }, false, {}, { { 0, 1, 0, 1 }, { 0, 1, 0, 4 }, { 0, 4, 1, 0 } }),
        sdef({ 0, 2, 4, 5, 7, 9, 10 }, false, {}, { { 0, 6, 3, 0 }, { 0, 3, 6, 0 }, { 0, 6, 0, 3 } }),
        sdef({ 0, 2, 3, 5, 7, 8, 11 }, true, {}, { { 0, 3, 4, 0 }, { 1, 4, 0, 0 }, { 0, 5, 4, 0 } }),
        sdef({ 0, 2, 3, 5, 7, 9, 11 }, true, {}, { { 0, 3, 4, 0 }, { 0, 4, 3, 0 } }),
        sdef({ 0, 2, 4, 7, 9 }, false, { 0, 2, 4, 5, 7, 9, 11 }),
        sdef({ 0, 3, 5, 7, 10 }, true, { 0, 2, 3, 5, 7, 8, 10 }),
        sdef({ 0, 2, 4, 6, 8, 10 }, false),
    };
    return v;
}

// chord quality tables by size 3/4/5
const IntVec& MAJQ(int n) { static const IntVec a { 0, 4, 7 }, b { 0, 4, 7, 11 }, c { 0, 4, 7, 11, 14 }; return n == 3 ? a : (n == 4 ? b : c); }
const IntVec& DOMQ(int n) { static const IntVec a { 0, 4, 7 }, b { 0, 4, 7, 10 }, c { 0, 4, 7, 10, 14 }; return n == 3 ? a : (n == 4 ? b : c); }
const IntVec& MINQ(int n) { static const IntVec a { 0, 3, 7 }, b { 0, 3, 7, 10 }, c { 0, 3, 7, 10, 14 }; return n == 3 ? a : (n == 4 ? b : c); }

enum class Q { Maj, Dom, Min };
const IntVec& quality(Q q, int n) { return q == Q::Maj ? MAJQ(n) : (q == Q::Dom ? DOMQ(n) : MINQ(n)); }

const std::vector<std::pair<int, Q>> kBorrowedMajor = { { 10, Q::Dom }, { 8, Q::Maj }, { 5, Q::Min }, { 3, Q::Maj } };
const std::vector<std::pair<int, Q>> kBorrowedMinor = { { 5, Q::Dom }, { 7, Q::Dom }, { 1, Q::Maj } };

const Pat SUSTAINED { { 0.0, 1.0 } };
const Pat HALVES { { 0.0, 0.5 }, { 0.5, 0.5 } };
const Pat QUARTERS { { 0.0, 0.25 }, { 0.25, 0.25 }, { 0.5, 0.25 }, { 0.75, 0.25 } };
const Pat EIGHTHS { { 0, .125 }, { .125, .125 }, { .25, .125 }, { .375, .125 }, { .5, .125 }, { .625, .125 }, { .75, .125 }, { .875, .125 } };
const Pat OFFBEAT { { 0.125, 0.125 }, { 0.375, 0.125 }, { 0.625, 0.125 }, { 0.875, 0.125 } };
const Pat SYNC { { 0.0, 0.1875 }, { 0.375, 0.1875 }, { 0.75, 0.25 } };
const Pat TRAPSTAB { { 0.0, 0.25 }, { 0.375, 0.125 }, { 0.625, 0.25 } };
const Pat SWING_PAT { { 0.0, 0.375 }, { 0.5, 0.5 } };

enum Rhythm { rQuarter, rEighth, rMixed, rPhrased };
enum Feel { fSmooth, fBalanced, fAdventurous, fSparse };
enum Contour { cFree, cAscending, cDescending, cArch, cWave };

struct GenreDef
{
    std::vector<IntVec> major, minor;
    IntVec sizes;
    std::vector<const Pat*> comp;
    int bass;          // 2 = Root, 3 = Smooth (matches the Bass option index)
    int rhythm, feel, contour;
    double sync;
};

const std::vector<GenreDef>& genres()
{
    static const std::vector<GenreDef> g = {
        // Pop
        { { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 5, 3, 0, 4 }, { 0, 3, 5, 4 }, { 3, 0, 4, 5 } },
          { { 0, 5, 2, 6 }, { 0, 6, 5, 6 }, { 5, 2, 6, 0 }, { 0, 3, 6, 2 } },
          { 3, 3, 4 }, { &SUSTAINED, &HALVES, &QUARTERS }, 2, rPhrased, fBalanced, cArch, 0.2 },
        // Hip-Hop / Trap
        { { { 5, 3, 0, 4 }, { 0, 5, 3, 4 }, { 5, 4, 3, 4 } },
          { { 0, 5, 2, 6 }, { 0, 6, 5, 6 }, { 0, 0, 5, 6 }, { 0, 2, 3, 6 }, { 0, 3, 5, 6 } },
          { 3, 4 }, { &SUSTAINED, &TRAPSTAB, &HALVES }, 2, rPhrased, fSparse, cWave, 0.45 },
        // Lo-fi
        { { { 1, 4, 0, 5 }, { 0, 5, 1, 4 }, { 3, 4, 2, 5 }, { 0, 2, 3, 4 } },
          { { 0, 3, 6, 2 }, { 5, 6, 0, 0 }, { 0, 5, 3, 6 }, { 1, 4, 0, 5 } },
          { 4, 5 }, { &SUSTAINED, &HALVES, &SWING_PAT }, 3, rPhrased, fSparse, cFree, 0.45 },
        // R&B
        { { { 0, 5, 1, 4 }, { 3, 2, 1, 4 }, { 1, 4, 0, 5 }, { 0, 2, 5, 3 } },
          { { 0, 3, 5, 6 }, { 5, 6, 0, 2 }, { 0, 5, 3, 4 }, { 1, 4, 0, 5 } },
          { 4, 5 }, { &SUSTAINED, &SWING_PAT, &HALVES }, 3, rPhrased, fBalanced, cWave, 0.45 },
        // EDM / House
        { { { 5, 3, 0, 4 }, { 0, 4, 5, 3 }, { 3, 4, 0, 5 }, { 0, 3, 4, 5 } },
          { { 0, 5, 2, 6 }, { 0, 6, 5, 4 }, { 5, 6, 0, 0 }, { 0, 3, 6, 5 } },
          { 3, 3, 4 }, { &OFFBEAT, &QUARTERS, &SUSTAINED }, 2, rEighth, fSmooth, cArch, 0.2 },
        // Jazz
        { { { 1, 4, 0, 0 }, { 0, 5, 1, 4 }, { 2, 5, 1, 4 }, { 1, 4, 0, 5 } },
          { { 1, 4, 0, 0 }, { 0, 3, 6, 2 }, { 5, 1, 4, 0 } },
          { 4, 5 }, { &SWING_PAT, &SUSTAINED, &HALVES }, 3, rPhrased, fAdventurous, cFree, 0.75 },
        // Rock
        { { { 0, 3, 4, 3 }, { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 0, 3, 0, 4 } },
          { { 0, 6, 3, 0 }, { 0, 5, 6, 0 }, { 0, 3, 4, 0 }, { 0, 6, 5, 6 } },
          { 3 }, { &HALVES, &EIGHTHS, &QUARTERS }, 2, rPhrased, fBalanced, cArch, 0.2 },
        // Cinematic
        { { { 0, 3, 0, 4 }, { 0, 5, 3, 4 }, { 0, 2, 3, 4 }, { 3, 0, 4, 5 } },
          { { 0, 5, 6, 0 }, { 0, 6, 5, 6 }, { 0, 2, 5, 6 }, { 0, 3, 5, 6 } },
          { 3, 4, 5 }, { &SUSTAINED, &SUSTAINED, &HALVES }, 2, rPhrased, fSmooth, cAscending, 0.0 },
        // Afrobeats
        { { { 0, 3, 4, 3 }, { 0, 5, 3, 4 }, { 5, 3, 0, 4 }, { 0, 4, 3, 4 } },
          { { 0, 5, 2, 6 }, { 0, 3, 6, 5 }, { 0, 6, 5, 6 }, { 5, 6, 0, 2 } },
          { 3, 4 }, { &SYNC, &HALVES, &OFFBEAT }, 3, rPhrased, fBalanced, cWave, 0.75 },
    };
    return g;
}

const int kFeelBias[4] = { 80, 60, 35, 70 };
const int kFeelRest[4] = { 5, 10, 15, 35 };
const double kSyncValues[4] = { 0.0, 0.2, 0.45, 0.75 };
const int kMajorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };

constexpr int MAX_ATTEMPTS = 150;
constexpr int TIER_SIZE = 50;
constexpr int MAX_CUSTOM_CHORDS = 16;

// ============================================================ small helpers
class Rng
{
public:
    explicit Rng(uint64_t seed) : eng(seed) {}
    double uni() { return std::uniform_real_distribution<double>(0.0, 1.0)(eng); }
    int randint(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(eng); }
    size_t index(size_t n) { return std::uniform_int_distribution<size_t>(0, n - 1)(eng); }
    template <class T> const T& choice(const std::vector<T>& v) { return v[index(v.size())]; }
    int pick(std::initializer_list<int> l) { return *(l.begin() + index(l.size())); }
    size_t weighted(const std::vector<double>& w) { std::discrete_distribution<size_t> d(w.begin(), w.end()); return d(eng); }

private:
    std::mt19937_64 eng;
};

int fitNote(int n) { while (n > 127) n -= 12; while (n < 0) n += 12; return n; }
int clampVel(int v) { return std::max(1, std::min(127, v)); }
int mod(int a, int m) { return ((a % m) + m) % m; }

IntVec buildChord(const IntVec& cs, int degree, int size)
{
    const int n = (int) cs.size();
    degree = mod(degree, n);
    IntVec offs;
    for (int i = 0; i < size; ++i)
    {
        const int idx = degree + 2 * i;
        offs.push_back(cs[idx % n] + 12 * (idx / n));
    }
    return offs;
}

IntVec rotateNotes(IntVec notes, int inv)
{
    std::sort(notes.begin(), notes.end());
    for (int i = 0; i < inv; ++i)
    {
        const int first = notes.front();
        notes.erase(notes.begin());
        notes.push_back(first + 12);
    }
    return notes;
}

int moveCost(const IntVec& prev, const IntVec& nw)
{
    int c = 0;
    for (int n : nw) { int m = 1 << 30; for (int p : prev) m = std::min(m, std::abs(n - p)); c += m; }
    for (int p : prev) { int m = 1 << 30; for (int n : nw) m = std::min(m, std::abs(p - n)); c += m; }
    return c;
}

int beatStrength(int t, int ppq)
{
    const int pos = mod(t, ppq * 4);
    if (pos == 0) return 4;
    if (pos == ppq * 2) return 3;
    if (pos % ppq == 0) return 2;
    const int half = ppq / 2;
    if (half > 0 && pos % half == 0) return 1;
    return 0;
}

double contourValue(int contour, double f)
{
    f = std::max(0.0, std::min(1.0, f));
    switch (contour)
    {
        case cAscending: return 0.1 + 0.85 * f;
        case cDescending: return 0.95 - 0.85 * f;
        case cArch: { const double peak = 0.65; const double g = f < peak ? f / peak : (1.0 - f) / (1.0 - peak); return 0.15 + 0.8 * g; }
        case cWave: return 0.5 + 0.35 * std::sin(2.0 * 3.14159265358979323846 * 2.0 * f);
        default: return 0.5;
    }
}

using Step = std::pair<int, int>;   // (onset tick, length)

std::vector<Step> phrasedRhythm(Rng& rng, int ppq, int total, double sync)
{
    const int unit = std::max(1, ppq / 4);
    const int durs[6] = { ppq / 4, ppq / 2, ppq, ppq * 3 / 2, ppq * 2, ppq * 3 };
    static const double base[5][6] = {
        { 3, 1, 0, 0, 0, 0 },          // strength 0
        { 0.8, 3, 1.5, 0.7, 0, 0 },    // 1
        { 0.3, 3, 3, 1.5, 0.7, 0 },    // 2
        { 0, 2, 3, 2, 2, 0 },          // 3
        { 0, 2, 3, 2, 3, 1 } };        // 4

    auto gen = [&](int t0, int t1) {
        std::vector<Step> out;
        int t = t0;
        while (t < t1)
        {
            const int st = beatStrength(t, ppq);
            const int remain = t1 - t;
            std::vector<int> opts;
            std::vector<double> ws;
            for (int k = 0; k < 6; ++k)
            {
                double w = base[st][k];
                const int d = durs[k];
                if (w <= 0 || d <= 0 || d > remain) continue;
                for (int p = t + unit; p < t + d; p += unit)
                    if (beatStrength(p, ppq) > st) { w *= sync * 3.0; break; }
                if (w > 0) { opts.push_back(d); ws.push_back(w); }
            }
            const int d = opts.empty() ? std::min(remain, unit) : opts[rng.weighted(ws)];
            out.push_back({ t, d });
            t += d;
        }
        return out;
    };

    const int phrase = ppq * 8;
    if (total < phrase * 2) return gen(0, total);
    const auto first = gen(0, phrase);
    std::vector<Step> steps = first;
    int pos = phrase;
    while (pos < total)
    {
        const int end = std::min(pos + phrase, total);
        if (rng.uni() < 0.7)
        {
            for (const auto& s : first)
                if (pos + s.first < end) steps.push_back({ pos + s.first, std::min(s.second, end - pos - s.first) });
        }
        else
        {
            const auto g = gen(pos, end);
            steps.insert(steps.end(), g.begin(), g.end());
        }
        pos = end;
    }
    return steps;
}

std::vector<Step> gridRhythm(Rng& rng, int style, int ppq, int total)
{
    IntVec choices;
    if (style == rQuarter) choices = { ppq };
    else if (style == rEighth) choices = { ppq / 2 };
    else choices = { ppq / 2, ppq / 2, ppq, ppq, ppq * 2 };
    std::vector<Step> steps;
    int pos = 0;
    while (pos < total)
    {
        const int len = std::min(rng.choice(choices), total - pos);
        steps.push_back({ pos, len });
        pos += len;
    }
    return steps;
}

int nearestIndex(const IntVec& pool, unsigned pcs, int cur, int window = 4)
{
    int best = -1, bestD = 0;
    for (int i = std::max(0, cur - window); i <= std::min((int) pool.size() - 1, cur + window); ++i)
        if (pcs & (1u << (pool[i] % 12)))
        {
            const int d = std::abs(i - cur);
            if (best < 0 || d < bestD) { best = i; bestD = d; }
        }
    return best >= 0 ? best : cur;
}

int snapToward(const IntVec& pool, unsigned pcs, int cur, int tgt, bool free)
{
    if (!free && tgt != cur)
    {
        const int d = tgt > cur ? 1 : -1;
        for (int k = 1; k < 5; ++k)
        {
            const int j = cur + d * k;
            if (j >= 0 && j < (int) pool.size() && (pcs & (1u << (pool[j] % 12)))) return j;
        }
    }
    return nearestIndex(pool, pcs, cur);
}

int nextChordTone(const IntVec& pool, unsigned pcs, int cur, int d)
{
    int j = cur + d;
    while (j >= 0 && j < (int) pool.size())
    {
        if (pcs & (1u << (pool[j] % 12))) return j;
        j += d;
    }
    return cur;
}

struct PrevMel { bool valid = false; int note = 0, top = 0; };

bool melodyOk(int p, const IntVec& chordNotes, const PrevMel& prev)
{
    for (int c : chordNotes)
        if (std::abs(p - c) == 1 || std::abs(p - c) == 13) return false;
    if (prev.valid)
    {
        const int top = *std::max_element(chordNotes.begin(), chordNotes.end());
        if (prev.note != p && prev.top != top && ((p > prev.note) == (top > prev.top)))
        {
            const int k1 = mod(prev.note - prev.top, 12), k2 = mod(p - top, 12);
            if (k1 == k2 && (k1 == 0 || k1 == 7)) return false;
        }
    }
    return true;
}

int clashFree(const IntVec& pool, int idx, const IntVec& chordNotes, const PrevMel& prev)
{
    if (melodyOk(pool[idx], chordNotes, prev)) return idx;
    for (int d : { 1, -1, 2, -2, 3, -3 })
    {
        const int j = idx + d;
        if (j >= 0 && j < (int) pool.size() && melodyOk(pool[j], chordNotes, prev)) return j;
    }
    return idx;
}

void splitTokens(const std::string& text, std::vector<std::string>& tokens)
{
    // split on whitespace , | -
    tokens.clear();
    std::string cur;
    for (char ch : text)
    {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',' || ch == '|' || ch == '-')
        {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        }
        else cur.push_back(ch);
    }
    if (!cur.empty()) tokens.push_back(cur);
}

bool allDigits(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }); }

ScaleDef customScale(const std::string& text, int key, std::vector<std::string>* msgs)
{
    std::set<int> notes { 0 };
    std::vector<std::string> bad, toks;
    splitTokens(text, toks);
    static const std::regex nameRe("^([A-Ga-g])([#b]?)$");
    static const std::map<char, int> pc = { { 'C', 0 }, { 'D', 2 }, { 'E', 4 }, { 'F', 5 }, { 'G', 7 }, { 'A', 9 }, { 'B', 11 } };
    for (const auto& tok : toks)
    {
        std::smatch m;
        if (allDigits(tok))
        {
            const int v = tok.size() < 4 ? std::atoi(tok.c_str()) : 99;
            if (v >= 0 && v <= 11) notes.insert(v); else bad.push_back(tok);
        }
        else if (std::regex_match(tok, m, nameRe))
        {
            int p = pc.at((char) std::toupper(m[1].str()[0]));
            if (m[2] == "#") p += 1; else if (m[2] == "b") p -= 1;
            notes.insert(mod(p - key, 12));
        }
        else bad.push_back(tok);
    }
    if (!bad.empty() && msgs)
    {
        std::string s = "Ignored unrecognised scale note(s):";
        for (auto& b : bad) s += " " + b;
        msgs->push_back(s);
    }
    if (notes.size() < 3)
    {
        if (msgs) msgs->push_back("Custom scale needs at least 3 notes; using Major.");
        return builtinScales()[0];
    }
    IntVec v(notes.begin(), notes.end());
    const bool minor = notes.count(3) && !notes.count(4);
    return sdef(v, minor);
}

ScaleDef pickScale(const Settings& S, const Texts& T, std::vector<std::string>* msgs)
{
    const int i = S[pScale];
    if (i < (int) builtinScales().size()) return builtinScales()[i];
    return customScale(T.customScale, S[pKey], msgs);
}

// ---------------------------------------------------------------- custom chord text
struct ChordSpec { IntVec offs; double beats; };   // beats < 0 means "use the default length"

bool chordFromToken(const std::string& core, const IntVec& cs, int size, IntVec& out)
{
    const int n = (int) cs.size();
    if (allDigits(core) && core.size() == 1 && core[0] >= '1' && core[0] <= '7')
    {
        out = buildChord(cs, core[0] - '1', size);
        return true;
    }
    static const std::regex numRe("^([b#]?)(VII|VI|IV|III|II|V|I|vii|vi|iv|iii|ii|v|i)(.*)$");
    static const std::map<std::string, int> roman = { { "i", 0 }, { "ii", 1 }, { "iii", 2 }, { "iv", 3 }, { "v", 4 }, { "vi", 5 }, { "vii", 6 } };
    const auto slashPos = core.find('/');
    const std::string left = slashPos == std::string::npos ? core : core.substr(0, slashPos);
    const std::string right = slashPos == std::string::npos ? "" : core.substr(slashPos + 1);

    std::smatch m;
    if (!std::regex_match(left, m, numRe)) return false;
    const std::string acc = m[1], num = m[2], rest = m[3];
    auto lower = [](std::string s) { for (auto& c : s) c = (char) std::tolower((unsigned char) c); return s; };
    const int deg = roman.at(lower(num));
    const int shift = acc == "b" ? -1 : (acc == "#" ? 1 : 0);

    int root;
    if (slashPos != std::string::npos)
    {
        std::smatch mr;
        if (!std::regex_match(right, mr, numRe) || !mr[3].str().empty()) return false;
        const std::string racc = mr[1];
        const int tr = cs[roman.at(lower(mr[2])) % n] + (racc == "b" ? -1 : (racc == "#" ? 1 : 0));
        root = tr + kMajorSteps[deg] + shift;
    }
    else if (!acc.empty()) root = kMajorSteps[deg] + shift;
    else root = cs[deg % n];
    root = mod(root, 12);

    auto has = [&](const char* s) { return rest.find(s) != std::string::npos; };
    const bool dim = has("dim") || has("\xC2\xB0");
    const bool aug = has("+") || has("aug");
    const bool maj = has("maj") || has("M7") || has("M9");
    const bool nine = has("9");
    const bool seven = nine || maj || has("7");
    const int sz = nine ? 5 : (seven ? 4 : size);
    const bool minor = std::islower((unsigned char) num[0]) != 0;

    IntVec iv;
    if (dim) iv = { 0, 3, 6, has("dim7") ? 9 : 10, 13 };
    else if (aug) iv = { 0, 4, 8, 10, 14 };
    else if (minor) iv = { 0, 3, 7, 10, 14 };
    else if (maj) iv = { 0, 4, 7, 11, 14 };
    else if (seven || deg == 4) iv = { 0, 4, 7, 10, 14 };
    else iv = { 0, 4, 7, 11, 14 };
    out.clear();
    for (int i = 0; i < sz; ++i) out.push_back(root + iv[i]);
    return true;
}

std::vector<ChordSpec> parseCustom(const std::string& text, const IntVec& cs, int size, std::vector<std::string>& bad)
{
    std::vector<ChordSpec> chords;
    std::vector<std::string> toks;
    splitTokens(text, toks);
    for (const auto& tok : toks)
    {
        const auto colon = tok.find(':');
        const std::string core = tok.substr(0, colon);
        double beats = -1;
        if (colon != std::string::npos)
        {
            char* endp = nullptr;
            const double b = std::strtod(tok.c_str() + colon + 1, &endp);
            if (endp != tok.c_str() + colon + 1) beats = std::max(0.5, std::min(16.0, b));
        }
        IntVec offs;
        if (!chordFromToken(core, cs, size, offs)) bad.push_back(tok);
        else if ((int) chords.size() < MAX_CUSTOM_CHORDS) chords.push_back({ offs, beats });
    }
    return chords;
}

uint64_t fnv(uint64_t h, uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xff; h *= 1099511628211ULL; } return h; }

// ============================================================ generator
class Generator
{
public:
    Generator(const Settings& s, const Texts& t, uint64_t seed, std::vector<std::string>& msgs)
        : S(sanitize(s)), rng(seed), messages(msgs), genre(genres()[S[pGenre]]), scale(pickScale(S, t, &msgs)),
          key(S[pKey]), root(12 * (S[pOctave] + 1) + S[pKey]), customText(t.customProgression)
    {
        if (S[pProgression] == 1)
        {
            std::vector<std::string> bad;
            const auto c = parseCustom(customText, scale.chords, 3, bad);
            if (!bad.empty())
            {
                std::string m = "Ignored unrecognised chord(s):";
                for (auto& b : bad) m += " " + b;
                messages.push_back(m);
            }
            if (!c.empty()) useCustom = true;
            else messages.push_back("No valid custom progression found; using Auto (genre).");
        }
    }

    Pattern attempt(int tier);
    Pattern scalePreview();

    Settings S;
private:
    Rng rng;
    std::vector<std::string>& messages;
    const GenreDef& genre;
    ScaleDef scale;
    int key, root;
    std::string customText;
    bool useCustom = false;

    int pickSize()
    {
        if (S[pChordType] == 0) return rng.choice(genre.sizes);
        return 2 + S[pChordType];   // 1->3, 2->4, 3->5
    }

    std::vector<ChordSpec> makeProgression(int size, bool& custom);
    void applyColor(std::vector<ChordSpec>& specs);
    void makeTiming(const std::vector<ChordSpec>& specs, IntVec& starts, IntVec& lengths, IntVec& shifts);
    std::vector<IntVec> voice(const std::vector<IntVec>& chords, bool vary, bool wide, int inversion);
    void chordEvents(std::vector<NoteEvent>& out, const std::vector<IntVec>& voicings, const IntVec& starts, const IntVec& lengths,
                     const IntVec& shifts, const std::vector<const Pat*>& patterns);
    void bassEvents(std::vector<NoteEvent>& out, const std::vector<IntVec>& chords, const IntVec& starts, const IntVec& lengths, const IntVec& shifts);
    void melodyEvents(std::vector<NoteEvent>& out, const std::vector<IntVec>& chords, const std::vector<IntVec>& voicings,
                      const IntVec& starts, const IntVec& lengths);
    void applySwing(std::vector<NoteEvent>& ev);
};

std::vector<ChordSpec> Generator::makeProgression(int size, bool& custom)
{
    custom = false;
    if (useCustom)
    {
        std::vector<std::string> bad;
        custom = true;
        return parseCustom(customText, scale.chords, size, bad);
    }
    const int p = S[pProgression];
    IntVec degrees;
    if (p >= 2) degrees = kFixedProgs[p - 2];
    else
    {
        std::vector<IntVec> pool = scale.minor ? genre.minor : genre.major;
        pool.insert(pool.end(), scale.sig.begin(), scale.sig.end());
        degrees = rng.choice(pool);
    }
    std::vector<ChordSpec> specs;
    for (int d : degrees) specs.push_back({ buildChord(scale.chords, d, size), -1.0 });
    return specs;
}

void Generator::applyColor(std::vector<ChordSpec>& specs)
{
    const int mode = S[pChordColor];
    if (mode == 0) return;
    const double p = S[pColorAmount] / 100.0;
    const int n = (int) specs.size();
    for (int i = n - 1; i > 0; --i)
    {
        if (rng.uni() >= p) continue;
        const bool canSec = (mode == 2 || mode == 3) && i < n - 1;
        const bool canBor = (mode == 1 || mode == 3);
        bool sec;
        if (canSec && canBor) sec = rng.index(2) == 0;
        else if (canSec) sec = true;
        else if (canBor) sec = false;
        else continue;
        const int size = std::max(3, std::min(5, (int) specs[i].offs.size()));
        int r;
        Q q;
        if (sec) { r = (specs[i + 1].offs[0] + 7) % 12; q = Q::Dom; }
        else
        {
            const auto& list = scale.minor ? kBorrowedMinor : kBorrowedMajor;
            const auto& pr = list[rng.index(list.size())];
            r = pr.first;
            q = pr.second;
        }
        IntVec offs;
        for (int k : quality(q, size)) offs.push_back(r + k);
        specs[i].offs = offs;
    }
}

void Generator::makeTiming(const std::vector<ChordSpec>& specs, IntVec& starts, IntVec& lengths, IntVec& shifts)
{
    const int ppq = PPQ;
    const int base = ppq * S[pChordLength];
    const int tmode = S[pChordTiming];
    const bool varied = tmode == 1 || tmode == 3, sync = tmode == 2 || tmode == 3;
    starts.clear(); lengths.clear(); shifts.clear();
    for (size_t i = 0; i < specs.size(); ++i)
    {
        int length;
        if (specs[i].beats >= 0) length = (int) (specs[i].beats * ppq);
        else if (varied)
        {
            static const double mult[5] = { 0.5, 1, 1, 1, 2 };
            length = (int) (base * mult[rng.index(5)]);
        }
        else length = base;
        length = std::max(ppq / 2, length);
        lengths.push_back(length);
        int sh = 0;
        if (sync && i > 0)
        {
            const double r = rng.uni();
            if (r < 0.35) sh = -(ppq / 2);
            else if (r < 0.55 && length > ppq) sh = ppq / 2;
        }
        shifts.push_back(sh);
    }
    int pos = 0;
    for (int l : lengths) { starts.push_back(pos); pos += l; }
}

std::vector<IntVec> Generator::voice(const std::vector<IntVec>& chords, bool vary, bool wide, int inversion)
{
    const int center = root + 6;
    std::vector<IntVec> out;
    IntVec prev;
    for (const auto& offs : chords)
    {
        IntVec base;
        for (int o : offs) base.push_back(root + o);
        const int size = (int) base.size();
        IntVec v;
        if (!vary) v = rotateNotes(base, std::max(0, std::min(inversion, size - 1)));
        else
        {
            std::vector<std::pair<double, IntVec>> cands;
            for (int inv = 0; inv < size; ++inv)
            {
                const IntVec r = rotateNotes(base, inv);
                for (int shift : { -12, 0, 12 })
                {
                    IntVec w;
                    double sum = 0;
                    for (int x : r) { w.push_back(x + shift); sum += x + shift; }
                    const double mean = sum / w.size();
                    if (std::abs(mean - center) > 9) continue;
                    const double cost = prev.empty() ? std::abs(mean - center) : (double) moveCost(prev, w);
                    cands.push_back({ cost, w });
                }
            }
            if (!cands.empty())
            {
                std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second < b.second; });
                const size_t lim = std::min(cands.size(), (size_t) (wide ? 8 : 3));
                v = cands[rng.index(lim)].second;
            }
            else v = rotateNotes(base, 0);
        }
        prev = v;
        IntVec fitted;
        for (int x : v) fitted.push_back(fitNote(x));
        out.push_back(fitted);
    }
    return out;
}

static std::vector<int> arpNotes(Rng& rng, int mode, const IntVec& notes, int octaves, int count)
{
    IntVec sorted = notes;
    std::sort(sorted.begin(), sorted.end());
    IntVec base;
    for (int o = 0; o < octaves; ++o) for (int n : sorted) base.push_back(n + 12 * o);
    IntVec out;
    if (mode == 4)
    {
        int last = -1000;
        for (int i = 0; i < count; ++i)
        {
            int n = rng.choice(base);
            if (base.size() > 1) while (n == last) n = rng.choice(base);
            out.push_back(n);
            last = n;
        }
        return out;
    }
    IntVec seq;
    if (mode == 1) seq = base;
    else if (mode == 2) seq.assign(base.rbegin(), base.rend());
    else
    {
        seq = base;
        for (int i = (int) base.size() - 2; i >= 1; --i) seq.push_back(base[i]);
    }
    for (int k = 0; k < count; ++k) out.push_back(seq[k % seq.size()]);
    return out;
}

void Generator::chordEvents(std::vector<NoteEvent>& out, const std::vector<IntVec>& voicings, const IntVec& starts, const IntVec& lengths,
                            const IntVec& shifts, const std::vector<const Pat*>& patterns)
{
    const int ppq = PPQ, vel0 = S[pVelocity], hum = S[pHumanize], strum = S[pStrum];
    const int arp = S[pArpeggio];
    const int steps[4] = { ppq, ppq / 2, ppq / 4, ppq / 8 };
    const int step = std::max(1, steps[S[pArpRate]]);
    const int octs = S[pArpOctaves];
    for (size_t i = 0; i < voicings.size(); ++i)
    {
        const int effStart = starts[i] + shifts[i];
        const int effLen = lengths[i] - shifts[i];
        const auto& v = voicings[i];
        for (const auto& fl : *patterns[i])
        {
            const int sT = effStart + (int) (fl.first * effLen);
            const int slot = std::max(ppq / 8, (int) (fl.second * effLen));
            if (arp)
            {
                const int count = std::max(1, slot / step);
                const auto seq = arpNotes(rng, arp, v, octs, count);
                for (int k = 0; k < (int) seq.size(); ++k)
                {
                    const int accent = k % 4 == 0 ? 8 : 0;
                    out.push_back({ fitNote(seq[k]), sT + k * step, std::max(1, (int) (step * 0.9)),
                                    clampVel(vel0 + accent + rng.randint(-hum, hum)), 0 });
                }
            }
            else
            {
                for (size_t ni = 0; ni < v.size(); ++ni)
                    out.push_back({ v[ni], sT + (int) ni * strum, std::max(ppq / 8, slot - (int) ni * strum),
                                    clampVel(vel0 + rng.randint(-hum, hum)), 0 });
            }
        }
    }
}

void Generator::bassEvents(std::vector<NoteEvent>& out, const std::vector<IntVec>& chords, const IntVec& starts, const IntVec& lengths, const IntVec& shifts)
{
    const int b = S[pBass];
    const int mode = b == 0 ? genre.bass : b;   // 1 Off, 2 Root, 3 Smooth
    if (mode == 1) return;
    const int low = root - 24;
    const int vel0 = S[pVelocity], hum = S[pHumanize];
    auto place = [&](int pc) { return low + mod(pc - low, 12); };
    bool havePrev = false;
    int prev = 0;
    for (size_t i = 0; i < chords.size(); ++i)
    {
        const auto& chord = chords[i];
        const int rootN = place((root + chord[0]) % 12);
        int note;
        if (mode == 2 || !havePrev) note = rootN;
        else
        {
            const int fifthN = place((root + chord[2]) % 12);
            std::set<int> cands;
            for (int n : { rootN, fifthN })
                for (int o : { -12, 0, 12 })
                    if (n + o >= low - 3 && n + o <= low + 15) cands.insert(n + o);
            IntVec ranked(cands.begin(), cands.end());
            std::sort(ranked.begin(), ranked.end(), [&](int x, int y) { return std::abs(x - prev) != std::abs(y - prev) ? std::abs(x - prev) < std::abs(y - prev) : x < y; });
            note = (ranked.size() == 1 || rng.uni() < 0.75) ? ranked[0] : ranked[1];
        }
        prev = note;
        havePrev = true;
        out.push_back({ fitNote(note), starts[i] + shifts[i], lengths[i] - shifts[i], clampVel(vel0 + rng.randint(-hum, hum)), 1 });
    }
}

void Generator::melodyEvents(std::vector<NoteEvent>& out, const std::vector<IntVec>& chords, const std::vector<IntVec>& voicings,
                             const IntVec& starts, const IntVec& lengths)
{
    const int ppq = PPQ;
    const int style = S[pMelodyRhythm] == 0 ? genre.rhythm : S[pMelodyRhythm] - 1;
    const int feel = S[pMelodyFeel] == 0 ? genre.feel : S[pMelodyFeel] - 1;
    const int bias = kFeelBias[feel], rest = kFeelRest[feel];
    const int contour = S[pMelodyContour] == 0 ? genre.contour : S[pMelodyContour] - 1;
    const double sync = S[pMelodySync] == 0 ? genre.sync : kSyncValues[S[pMelodySync] - 1];
    const double leap = S[pLeaps] / 100.0;
    const bool allowTension = S[pTension] != 0;
    const int vel0 = S[pVelocity], hum = S[pHumanize];
    static const int accent[5] = { -7, -3, 2, 6, 10 };

    const int base = 12 * (S[pMelodyOctave] + 1) + key;
    std::set<int> poolSet;
    for (int o : { 0, 12 }) for (int s : scale.notes) { const int n = base + o + s; if (n >= 0 && n <= 127) poolSet.insert(n); }
    const IntVec pool(poolSet.begin(), poolSet.end());
    const int n = (int) pool.size();
    if (n == 0) return;
    const int total = starts.back() + lengths.back();
    const int lo = 1, hi = std::max(1, n - 2);
    const bool free = contour == cFree;

    auto target = [&](int t) { return (int) std::lround(lo + (hi - lo) * contourValue(contour, (double) t / total)); };
    auto clampi = [&](int i) { return std::max(0, std::min(n - 1, i)); };

    std::vector<Step> onsets;
    if (style == rPhrased) onsets = phrasedRhythm(rng, ppq, total, sync);
    else
        for (size_t ci = 0; ci < chords.size(); ++ci)
            for (const auto& s : gridRhythm(rng, style, ppq, lengths[ci])) onsets.push_back({ starts[ci] + s.first, s.second });

    auto chordAt = [&](int t) { int ci = 0; for (size_t i = 0; i < starts.size(); ++i) if (starts[i] <= t) ci = (int) i; return ci; };

    int idx = clampi(target(0));
    int recover = 0, figLeft = 0, figDir = 1;
    std::vector<bool> seenChords(chords.size(), false);
    PrevMel prev;
    for (size_t k = 0; k < onsets.size(); ++k)
    {
        int t = onsets[k].first, length = onsets[k].second;
        const int ci = chordAt(t);
        unsigned pcs = 0;
        for (int o : chords[ci]) pcs |= 1u << ((key + o) % 12);
        const int st = beatStrength(t, ppq);
        const int tgt = target(t);
        const bool first = !seenChords[ci];
        seenChords[ci] = true;
        const bool lastOnset = k + 1 == onsets.size();

        if (!first && st < 3 && !lastOnset && rng.randint(1, 100) <= rest) continue;

        if (first) { idx = snapToward(pool, pcs, idx, tgt, free); recover = 0; figLeft = 0; }
        else if (lastOnset && ci == (int) chords.size() - 1) idx = nearestIndex(pool, 1u << (key % 12), idx, 6);
        else if (figLeft > 0)
        {
            const int j = nextChordTone(pool, pcs, idx, figDir);
            figLeft = j != idx ? figLeft - 1 : 0;
            idx = j;
        }
        else if (recover != 0) { idx = clampi(idx + recover); recover = 0; }
        else if (leap > 0 && st >= 2 && rng.uni() < leap)
        {
            int d;
            if (tgt != idx && rng.uni() < 0.65) d = tgt > idx ? 1 : -1;
            else d = rng.pick({ -1, 1 });
            if (rng.uni() < 0.5)
            {
                const int j = nextChordTone(pool, pcs, idx, d);
                if (j != idx) { idx = j; figDir = d; figLeft = rng.pick({ 1, 2 }); }
            }
            else
            {
                const int goal = pool[idx] + d * rng.pick({ 5, 7, 12 });
                int best = 0;
                for (int i = 1; i < n; ++i) if (std::abs(pool[i] - goal) < std::abs(pool[best] - goal)) best = i;
                idx = best;
                recover = -d;
            }
        }
        else
        {
            double eff;
            if (st >= 3) eff = 0.9;
            else if (st == 2) eff = bias / 100.0;
            else if (st == 1) eff = bias / 200.0;
            else eff = bias / 300.0;
            if (rng.uni() < eff) idx = snapToward(pool, pcs, idx, tgt, free);
            else
            {
                const int dist = tgt - idx;
                const double half = std::max(1.0, n / 2.0);
                const double p = free ? std::min(0.9, std::abs(dist) / half) : std::min(0.9, 0.4 + std::abs(dist) / half);
                int step;
                if (dist != 0 && rng.uni() < p) step = rng.pick({ 0, 1, 1, 2 }) * (dist > 0 ? 1 : -1);
                else step = rng.pick({ -2, -1, -1, 0, 1, 1, 2 });
                idx = clampi(idx + step);
            }
        }

        if (!allowTension) idx = clashFree(pool, idx, voicings[ci], prev);
        prev = { true, pool[idx], *std::max_element(voicings[ci].begin(), voicings[ci].end()) };

        if (!allowTension)
        {
            const int end = t + length;
            for (size_t cj = ci + 1; cj < chords.size(); ++cj)
            {
                if (starts[cj] >= end) break;
                bool clash = false;
                for (int c : voicings[cj]) if (std::abs(pool[idx] - c) == 1 || std::abs(pool[idx] - c) == 13) clash = true;
                if (clash) { length = std::max(1, starts[cj] - t); break; }
            }
        }
        out.push_back({ pool[idx], t, length, clampVel(vel0 - 10 + accent[st] + rng.randint(-hum, hum)), 2 });
    }
}

void Generator::applySwing(std::vector<NoteEvent>& ev)
{
    const int swing = S[pSwing];
    if (swing <= 0) return;
    const int unit = S[pSwingGrid] == 0 ? PPQ / 2 : PPQ / 4;
    const int shift = (int) ((unit / 3.0) * (swing / 100.0));
    for (auto& e : ev)
        if (e.tick % unit == 0 && (e.tick / unit) % 2 == 1) e.tick += shift;
}

Pattern Generator::attempt(int tier)
{
    const int mode = S[pOutput];
    const bool wantChords = mode == 0 || mode == 1, wantMelody = mode == 0 || mode == 2;

    const int size = pickSize();
    bool custom;
    auto specs = makeProgression(size, custom);
    if (!custom) applyColor(specs);
    std::vector<IntVec> chords;
    for (const auto& sp : specs) chords.push_back(sp.offs);
    IntVec starts, lengths, shifts;
    makeTiming(specs, starts, lengths, shifts);

    const bool vary = S[pChordVariation] != 0 || tier >= 1;
    const bool wide = tier >= 2;
    const auto voicings = voice(chords, vary, wide, S[pInversion]);

    std::vector<const Pat*> patterns;
    if (wide) for (size_t i = 0; i < chords.size(); ++i) patterns.push_back(genre.comp[rng.index(genre.comp.size())]);
    else { const Pat* p = genre.comp[rng.index(genre.comp.size())]; patterns.assign(chords.size(), p); }

    Pattern pat;
    if (wantChords)
    {
        chordEvents(pat.events, voicings, starts, lengths, shifts, patterns);
        bassEvents(pat.events, chords, starts, lengths, shifts);
    }
    if (wantMelody) melodyEvents(pat.events, chords, voicings, starts, lengths);
    applySwing(pat.events);

    const int total = starts.back() + lengths.back();
    const int bar = PPQ * 4;
    pat.loopTicks = std::max(bar, ((total + bar - 1) / bar) * bar);
    return pat;
}

Pattern Generator::scalePreview()
{
    Pattern pat;
    const int r = 12 * (S[pOctave] + 1) + key;
    IntVec notes = scale.notes;
    notes.push_back(12);
    for (size_t k = 0; k < notes.size(); ++k)
        pat.events.push_back({ fitNote(r + notes[k]), (int) k * PPQ, PPQ, clampVel(S[pVelocity]), 0 });
    const int bar = PPQ * 4;
    pat.loopTicks = std::max(bar, (((int) notes.size() * PPQ + bar - 1) / bar) * bar);
    return pat;
}
} // namespace

// ============================================================ public API
uint64_t fingerprint(const std::vector<NoteEvent>& events)
{
    std::vector<std::array<int, 3>> core;
    for (const auto& e : events) core.push_back({ e.note, e.tick, e.length });
    std::sort(core.begin(), core.end());
    uint64_t h = 1469598103934665603ULL;
    for (const auto& c : core) { h = fnv(h, (uint64_t) c[0]); h = fnv(h, (uint64_t) c[1]); h = fnv(h, (uint64_t) c[2]); }
    return h;
}

ScaleView resolveScale(const Settings& s, const Texts& t, std::vector<std::string>* messages)
{
    const auto sc = pickScale(sanitize(s), t, messages);
    return { sc.notes, sc.chords, sc.minor };
}

static uint32_t settingsKey(const Settings& s, const Texts& t)
{
    uint64_t h = 1469598103934665603ULL;
    for (int v : s) h = fnv(h, (uint64_t) (int64_t) v);
    if (s[pScale] == (int) kScaleLabels.size() - 1) for (char c : t.customScale) h = fnv(h, (uint64_t) c);
    if (s[pProgression] == 1) for (char c : t.customProgression) h = fnv(h, (uint64_t) c);
    return (uint32_t) (h ^ (h >> 32));
}

GenerateResult generate(const Settings& settings, const Texts& texts, History* history, uint64_t seed)
{
    GenerateResult res;
    Generator gen(settings, texts, seed, res.messages);

    if (gen.S[pOutput] == 3)
    {
        res.pattern = gen.scalePreview();
        return res;
    }

    const uint32_t skey = settingsKey(gen.S, texts);
    auto search = [&](Pattern& out, uint64_t& sig) {
        for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
        {
            out = gen.attempt(std::min(2, attempt / TIER_SIZE));
            sig = fingerprint(out.events);
            if (!history || !history->contains(sig)) return true;
        }
        return false;
    };

    uint64_t sig = 0;
    bool fresh = search(res.pattern, sig);
    if (!fresh && history)
    {
        history->pruneOldestHalf(skey);
        res.messages.push_back("All variations for these settings were used; cleared the oldest half of them.");
        fresh = search(res.pattern, sig);
    }
    res.fresh = fresh;
    if (history) history->add(skey, sig);
    return res;
}

// ============================================================ history
History::History(std::string filePath) : path(std::move(filePath)) {}

void History::load()
{
    entries.clear();
    seen.clear();
    repaired = 0;
    if (path.empty() || !std::filesystem::exists(path)) return;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream ss(line);
        std::string a, b, extra;
        if (!(ss >> a >> b) || (ss >> extra) || a.size() != 8 || b.size() != 16 ||
            a.find_first_not_of("0123456789abcdef") != std::string::npos ||
            b.find_first_not_of("0123456789abcdef") != std::string::npos)
        {
            if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos) ++repaired;
            continue;
        }
        const uint32_t k = (uint32_t) std::stoul(a, nullptr, 16);
        const uint64_t s = std::stoull(b, nullptr, 16);
        entries.push_back({ k, s });
        seen.insert(s);
    }
    if (repaired > 0) rewrite();
}

void History::rewrite()
{
    if (path.empty()) return;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        char buf[64];
        for (const auto& e : entries) { std::snprintf(buf, sizeof buf, "%08x %016llx\n", e.first, (unsigned long long) e.second); out << buf; }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(path, ec); std::filesystem::rename(tmp, path, ec); }
}

void History::add(uint32_t settingsKey, uint64_t sig)
{
    entries.push_back({ settingsKey, sig });
    seen.insert(sig);
    if (path.empty()) return;
    std::ofstream out(path, std::ios::app);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%08x %016llx\n", settingsKey, (unsigned long long) sig);
    out << buf;
}

void History::pruneOldestHalf(uint32_t settingsKey)
{
    std::vector<size_t> mine;
    for (size_t i = 0; i < entries.size(); ++i) if (entries[i].first == settingsKey) mine.push_back(i);
    if (mine.empty()) for (size_t i = 0; i < entries.size(); ++i) mine.push_back(i);
    std::set<size_t> drop;
    for (size_t i = 0; i < (mine.size() + 1) / 2; ++i) drop.insert(mine[i]);
    std::vector<std::pair<uint32_t, uint64_t>> kept;
    for (size_t i = 0; i < entries.size(); ++i) if (!drop.count(i)) kept.push_back(entries[i]);
    entries = kept;
    seen.clear();
    for (const auto& e : entries) seen.insert(e.second);
    rewrite();
}

void History::clear()
{
    entries.clear();
    seen.clear();
    rewrite();
}
} // namespace cg
