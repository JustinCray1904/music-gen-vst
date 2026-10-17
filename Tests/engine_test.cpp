// Standalone tests for the ChordGen engine (no JUCE needed).
#include "../Source/Engine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <random>
#include <set>

using namespace cg;

static std::mt19937_64 g_rng(12345);
static uint64_t seed() { return g_rng(); }

static int strength(int t)
{
    const int pos = ((t % (PPQ * 4)) + PPQ * 4) % (PPQ * 4);
    if (pos == 0) return 4;
    if (pos == PPQ * 2) return 3;
    if (pos % PPQ == 0) return 2;
    if (pos % (PPQ / 2) == 0) return 1;
    return 0;
}

static Pattern gen(const Settings& s, const Texts& t = {}, History* h = nullptr)
{
    return generate(s, t, h, seed()).pattern;
}

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++failures; };

    // 1. key lock + validity across everything
    {
        long notes = 0, bad = 0, invalid = 0;
        std::mt19937 r(7);
        const auto& d = paramDescs();
        for (int scale = 0; scale < 12; ++scale)
            for (int key = 0; key < 12; ++key)
                for (int genre = 0; genre < 9; ++genre)
                    for (int output = 0; output < 3; ++output)
                        for (int oct : { 1, 4, 7 })
                        {
                            Settings s = defaultSettings();
                            s[pScale] = scale; s[pKey] = key; s[pGenre] = genre; s[pOutput] = output; s[pOctave] = oct;
                            s[pMelodyOctave] = oct == 1 ? 2 : (oct == 4 ? 5 : 8);
                            s[pBass] = r() % 4; s[pArpeggio] = r() % 5; s[pChordTiming] = r() % 4; s[pChordType] = r() % 4;
                            s[pArpOctaves] = 1 + r() % 3; s[pSwing] = (r() % 2) * 60;
                            Texts t; t.customScale = scale == 11 ? ((key % 2) ? "C D Eb F G Ab B" : "0 2 4 7 9") : "";
                            const auto view = resolveScale(s, t);
                            unsigned allowed = 0;
                            for (int c : view.chords) allowed |= 1u << ((key + c) % 12);
                            const auto p = gen(s, t);
                            for (const auto& e : p.events)
                            {
                                ++notes;
                                if (!(allowed & (1u << (e.note % 12))) || e.note < 0 || e.note > 127) ++bad;
                                if (e.vel < 1 || e.vel > 127 || e.length < 1 || e.tick < 0 || e.channel < 0 || e.channel > 2) ++invalid;
                            }
                        }
        (void) d;
        std::printf("1. KEY LOCK %ld notes: out-of-key/out-of-range %ld, invalid fields %ld\n", notes, bad, invalid);
        check(bad == 0 && invalid == 0, "every note in key, valid MIDI, valid fields");
    }

    // 2. custom progression text
    {
        Settings s = defaultSettings();
        s[pGenre] = 7; s[pProgression] = 1; s[pOutput] = 1; s[pBass] = 1; s[pChordVariation] = 0; s[pHumanize] = 0; s[pChordType] = 1;
        Texts t; t.customProgression = "ii7 V7 Imaj7";
        auto p = gen(s, t);
        std::set<int> pcs;
        for (const auto& e : p.events) if (e.tick == 0) pcs.insert(e.note % 12);
        check(pcs == std::set<int>({ 2, 5, 9, 0 }), "'ii7' in C major = D F A C");
        t.customProgression = "I:1 V:3";
        s[pChordType] = 0;
        p = gen(s, t);
        std::set<int> starts;
        for (const auto& e : p.events) starts.insert(e.tick);
        check(starts.count(0) && starts.count(PPQ) && !starts.count(PPQ * 2), "per-chord beats 'I:1 V:3' (second chord at tick 96)");
        t.customProgression = "V7/vi";
        s[pChordType] = 1;
        p = gen(s, t);
        pcs.clear();
        for (const auto& e : p.events) if (e.tick == 0) pcs.insert(e.note % 12);
        check(pcs == std::set<int>({ 4, 8, 11, 2 }), "'V7/vi' in C = E G# B D");
        t.customProgression = "zzz ???";
        auto r = generate(s, t, nullptr, seed());
        bool warned = false;
        for (auto& m : r.messages) if (m.find("zzz") != std::string::npos || m.find("No valid") != std::string::npos) warned = true;
        check(warned && !r.pattern.events.empty(), "bad custom text warns and falls back to Auto");
    }

    // 3. never repeats (incl. fixed progression, variation off)
    {
        History h;
        std::set<uint64_t> sigs;
        Settings s = defaultSettings();
        s[pOutput] = 1; s[pProgression] = 2; s[pChordVariation] = 0;
        for (int i = 0; i < 500; ++i) sigs.insert(fingerprint(gen(s, {}, &h).events));
        std::printf("3. chords only, fixed progression, variation OFF: %zu unique of 500\n", sigs.size());
        check(sigs.size() == 500, "500/500 unique");
        sigs.clear();
        History h2;
        Settings s2 = defaultSettings();
        for (int i = 0; i < 300; ++i) sigs.insert(fingerprint(gen(s2, {}, &h2).events));
        check(sigs.size() == 300, "chords + bass + melody 300/300 unique");
    }

    // 4. history file: persistence, repair, prune
    {
        const std::string path = (std::filesystem::temp_directory_path() / "cg_hist_test.txt").string();
        std::filesystem::remove(path);
        { History h(path); for (int i = 0; i < 40; ++i) h.add(0xabcdu, 1000 + i); }
        History h2(path); h2.load();
        check(h2.size() == 40 && h2.contains(1005), "history survives a reload");
        { std::ofstream o(path, std::ios::app); o << "garbage line\nxyz 12\n0000abcd"; }
        History h3(path); h3.load();
        check(h3.size() == 40 && h3.repairedLines() == 3, "3 damaged lines repaired, 40 valid kept");
        History h4(path); h4.load();
        check(h4.size() == 40 && h4.repairedLines() == 0, "file was cleaned on disk");
        h4.pruneOldestHalf(0xabcdu);
        History h5(path); h5.load();
        check(h5.size() == 20 && !h5.contains(1000) && h5.contains(1039), "prune drops the oldest half, persisted");
        h5.clear();
        History h6(path); h6.load();
        check(h6.size() == 0, "clear empties the file");
        std::filesystem::remove(path);
        // forced exhaustion: pre-fill history with everything one tiny space can make
        Settings s = defaultSettings();
        s[pOutput] = 1; s[pProgression] = 2; s[pChordVariation] = 0; s[pGenre] = 7; s[pChordType] = 1; s[pBass] = 1; s[pHumanize] = 0;
        History h7;
        int pruned = 0;
        for (int i = 0; i < 2500; ++i)
        {
            auto r = generate(s, {}, &h7, seed());
            for (auto& m : r.messages) if (m.find("oldest half") != std::string::npos) ++pruned;
            if (r.pattern.events.empty()) { check(false, "empty pattern"); break; }
        }
        std::printf("   2500 runs in one tiny settings space: prune events = %d, no crash\n", pruned);
    }

    // 5. melody vs chords: clashes and parallels with tension off/on
    {
        auto count = [&](int tension) {
            long clashes = 0, mel = 0;
            for (int run = 0; run < 200; ++run)
            {
                Settings s = defaultSettings();
                s[pTension] = tension; s[pChordType] = 3; s[pChordColor] = 3; s[pColorAmount] = 60; s[pBass] = 1;
                s[pProgression] = 2; s[pChordTiming] = 0; s[pSwing] = 0;
                const auto p = gen(s);
                for (const auto& m : p.events)
                {
                    if (m.channel != 2) continue;
                    ++mel;
                    for (const auto& c : p.events)
                        if (c.channel == 0 && c.tick <= m.tick && m.tick < c.tick + c.length &&
                            (std::abs(m.note - c.note) == 1 || std::abs(m.note - c.note) == 13)) { ++clashes; break; }
                }
            }
            return std::make_pair(clashes, mel);
        };
        const auto off = count(0), on = count(1);
        std::printf("5. minor-2nd/9th clashes with sounding chord notes: tension OFF %ld/%ld, ON %ld/%ld\n", off.first, off.second, on.first, on.second);
        check(off.first == 0 && on.first > 0, "avoidance works; allowing tension brings them back");
    }

    // 6. rhythm: syncopation control, bar-line ties, strong beats
    {
        auto stats = [&](int sync) {
            long notes = 0, syncd = 0, barCross = 0;
            for (int run = 0; run < 300; ++run)
            {
                Settings s = defaultSettings();
                s[pOutput] = 2; s[pMelodyRhythm] = 4; s[pMelodySync] = sync; s[pLeaps] = 0; s[pSwing] = 0; s[pProgression] = 2;
                const auto p = gen(s);
                for (const auto& e : p.events)
                {
                    ++notes;
                    const int st = strength(e.tick);
                    for (int q = e.tick + PPQ / 4; q < e.tick + e.length; q += PPQ / 4) if (strength(q) > st) { ++syncd; break; }
                    if (e.tick / (PPQ * 4) != (e.tick + e.length - 1) / (PPQ * 4)) ++barCross;
                }
            }
            return std::make_pair(100.0 * syncd / notes, 100.0 * barCross / notes);
        };
        const char* names[] = { "", "None", "Light", "Medium", "Heavy" };
        double none = 0, heavy = 0;
        for (int sync = 1; sync <= 4; ++sync)
        {
            const auto r = stats(sync);
            std::printf("6. syncopation %-6s: syncopated notes %.1f%%, tied across a bar line %.1f%%\n", names[sync], r.first, r.second);
            if (sync == 1) none = r.first;
            if (sync == 4) heavy = r.first;
        }
        check(none == 0.0 && heavy > 10.0, "None = zero syncopation, Heavy = plenty");
    }

    // 7. contour shape
    {
        auto shape = [&](int contour) {
            std::vector<double> sum(6, 0), cnt(6, 0);
            for (int run = 0; run < 150; ++run)
            {
                Settings s = defaultSettings();
                s[pOutput] = 2; s[pMelodyContour] = contour; s[pProgression] = 2;
                const auto p = gen(s);
                for (const auto& e : p.events) { const int b = std::min(5, e.tick * 6 / p.loopTicks); sum[b] += e.note; cnt[b] += 1; }
            }
            std::vector<double> o;
            for (int i = 0; i < 6; ++i) o.push_back(sum[i] / std::max(1.0, cnt[i]));
            return o;
        };
        const char* names[] = { "", "Free", "Ascending", "Descending", "Arch", "Wave" };
        std::map<int, std::vector<double>> sh;
        for (int c = 1; c <= 5; ++c)
        {
            sh[c] = shape(c);
            std::printf("7. %-10s mean pitch by time slice:", names[c]);
            for (double v : sh[c]) std::printf(" %5.1f", v);
            std::printf("\n");
        }
        check(sh[2].back() > sh[2].front() + 4, "Ascending rises");
        check(sh[3].front() > sh[3].back() + 4, "Descending falls");
        check(sh[4][3] > sh[4][0] + 4 && sh[4][3] > sh[4][5] + 2, "Arch builds then falls");
    }

    // 8. leaps
    {
        auto big = [&](int leaps) {
            long tot = 0, bigMoves = 0;
            for (int run = 0; run < 200; ++run)
            {
                Settings s = defaultSettings();
                s[pOutput] = 2; s[pLeaps] = leaps; s[pMelodyContour] = 1; s[pProgression] = 2;
                auto p = gen(s);
                std::sort(p.events.begin(), p.events.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
                for (size_t i = 1; i < p.events.size(); ++i) { ++tot; bigMoves += std::abs(p.events[i].note - p.events[i - 1].note) >= 5; }
            }
            return 100.0 * bigMoves / tot;
        };
        const double a = big(0), b = big(60);
        std::printf("8. moves of a fourth or more: leaps 0%% -> %.1f%%, leaps 60%% -> %.1f%%\n", a, b);
        check(b > a * 2, "leap control works");
    }

    // 9. bass
    {
        long outside = 0, total = 0;
        double jumpRoot = 0, jumpSmooth = 0; long nRoot = 0, nSmooth = 0;
        for (int mode : { 2, 3 })
            for (int run = 0; run < 200; ++run)
            {
                Settings s = defaultSettings();
                s[pOutput] = 1; s[pBass] = mode; s[pGenre] = 5; s[pProgression] = 2;
                auto p = gen(s);
                std::vector<NoteEvent> bass;
                for (auto& e : p.events) if (e.channel == 1) bass.push_back(e);
                std::sort(bass.begin(), bass.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
                for (auto& e : bass) { ++total; if (e.note < 33 || e.note > 48) ++outside; }
                for (size_t i = 1; i < bass.size(); ++i)
                {
                    const double j = std::abs(bass[i].note - bass[i - 1].note);
                    if (mode == 2) { jumpRoot += j; ++nRoot; } else { jumpSmooth += j; ++nSmooth; }
                }
            }
        std::printf("9. bass: register violations %ld/%ld, mean jump Root %.1f vs Smooth %.1f semitones\n", outside, total, jumpRoot / nRoot, jumpSmooth / nSmooth);
        check(outside == 0 && jumpSmooth / nSmooth < jumpRoot / nRoot, "bass stays in register; Smooth moves less than Root");
        Settings s = defaultSettings(); s[pOutput] = 1; s[pBass] = 1;
        long bassNotes = 0;
        for (auto& e : gen(s).events) bassNotes += e.channel == 1;
        check(bassNotes == 0, "Bass Off = no bass notes");
    }

    // 10. inversion bounds, scale preview, sanitising
    {
        Settings s = defaultSettings();
        s[pOutput] = 1; s[pBass] = 1; s[pChordVariation] = 0; s[pGenre] = 7; s[pProgression] = 2; s[pChordType] = 1; s[pHumanize] = 0;
        s[pInversion] = 3;
        const auto p3 = gen(s);
        s[pInversion] = 2;
        const auto p2 = gen(s);
        std::set<int> a, b;
        for (auto& e : p3.events) if (e.tick == 0) a.insert(e.note);
        for (auto& e : p2.events) if (e.tick == 0) b.insert(e.note);
        check(!a.empty() && !b.empty() && *a.rbegin() - *a.begin() < 12 + 12, "inversion knob clamped to the chord's real inversions");
        Settings sp = defaultSettings();
        sp[pOutput] = 3; sp[pScale] = 2; sp[pKey] = 2;
        std::string names; const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        auto pv = gen(sp);
        std::sort(pv.events.begin(), pv.events.end(), [](auto& x, auto& y) { return x.tick < y.tick; });
        for (auto& e : pv.events) names += std::string(nn[e.note % 12]) + " ";
        std::printf("10. scale preview D Dorian: %s\n", names.c_str());
        check(names == "D E F G A B C D ", "scale preview is D Dorian");
        Settings crazy = defaultSettings();
        for (auto& v : crazy) v = 9999;
        check(!gen(crazy).events.empty(), "out-of-range settings are clamped, not fatal");
        Settings neg = defaultSettings();
        for (auto& v : neg) v = -5;
        check(!gen(neg).events.empty(), "negative settings are clamped, not fatal");
    }

    // 11. speed
    {
        Settings s = defaultSettings();
        History h;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 300; ++i) generate(s, {}, &h, seed());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 300;
        std::printf("11. average generate time: %.3f ms\n", ms);
        check(ms < 20, "fast enough to run on the UI thread");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
