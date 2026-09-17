// ProgramSignals.hpp — deterministic stand-ins for the kinds of programme the
// Space surround choices have to work on, generated from first principles (no
// recordings): a pop mix, a ballad, an electronic track with out-of-phase width
// and a mono podcast. Each is a stereo pair of floats at a given rate.
//
// What matters for the surround tests is the spatial make-up, so each one is
// built the way that genre is usually mixed:
//   - pop: kick, snare, bass and lead vocal in the centre, hi-hats a little to
//     the right, two separately played guitars hard left and hard right, and a
//     short stereo reverb on the vocal and snare;
//   - ballad: vocal centre, a piano spread low-left to high-right, a wide string
//     pad of independently drifting voices and a long reverb;
//   - electronic: four-on-the-floor kick and sub bass centred, a detuned stereo
//     pad, and a width effect that is partly out of phase between the sides;
//   - podcast: one voice with pauses, identical in both channels.
#ifndef ROOMCUT_PROGRAM_SIGNALS_HPP
#define ROOMCUT_PROGRAM_SIGNALS_HPP

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace roomcut::programs {

struct Stereo { std::vector<float> left, right; };

namespace detail {

constexpr double kPi = 3.14159265358979323846;

struct Rng {
    uint32_t state;
    double next() {   // uniform -1..1
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<double>(state & 0xffffff) / static_cast<double>(0xffffff) * 2.0 - 1.0;
    }
};

// Resonant band-pass (RBJ constant peak gain), for formants and noise colours.
struct Resonator {
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(double fs, double hz, double q) {
        const double w = 2 * kPi * hz / fs, alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
        b0 = alpha / a0; b2 = -alpha / a0; a1 = -2 * std::cos(w) / a0; a2 = (1 - alpha) / a0;
    }
    double process(double x) {
        const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

struct OnePole {
    double a = 0, y = 0;
    void lowpass(double fs, double hz) { a = 1 - std::exp(-2 * kPi * hz / fs); }
    double process(double x) { return y += a * (x - y); }
};

// A small Schroeder reverb: four combs into two all-passes. Left and right use
// different delay sets, so their tails are decorrelated.
struct Reverb {
    std::array<std::vector<double>, 4> comb;
    std::array<std::size_t, 4> combAt{};
    std::array<double, 4> combLp{};
    std::array<std::vector<double>, 2> allpass;
    std::array<std::size_t, 2> allpassAt{};
    double feedback = 0.8;
    void prepare(double fs, const std::array<double, 4>& combMs, const std::array<double, 2>& allpassMs, double rt60) {
        for (std::size_t i = 0; i < 4; ++i) comb[i].assign(static_cast<std::size_t>(fs * combMs[i] * 0.001), 0.0);
        for (std::size_t i = 0; i < 2; ++i) allpass[i].assign(static_cast<std::size_t>(fs * allpassMs[i] * 0.001), 0.0);
        feedback = std::pow(10.0, -3.0 * (combMs[1] * 0.001) / rt60);
    }
    double process(double x) {
        double sum = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            double& slot = comb[i][combAt[i]];
            combLp[i] += 0.4 * (slot - combLp[i]);
            sum += slot;
            slot = x + combLp[i] * feedback;
            combAt[i] = (combAt[i] + 1) % comb[i].size();
        }
        double y = sum * 0.25;
        for (std::size_t i = 0; i < 2; ++i) {
            double& slot = allpass[i][allpassAt[i]];
            const double out = -0.5 * y + slot;
            slot = y + 0.5 * out;
            allpassAt[i] = (allpassAt[i] + 1) % allpass[i].size();
            y = out;
        }
        return y;
    }
};

// A sung or spoken voice: a glottal pulse train with vibrato through three
// formants, gated into syllables. `sung` picks steady pitch lines over speech.
struct Voice {
    double fs = 48000, phase = 0;
    Resonator f1, f2, f3;
    Rng breath{97};
    void prepare(double rate) {
        fs = rate;
        f1.set(fs, 700, 6); f2.set(fs, 1220, 8); f3.set(fs, 2600, 10);
    }
    double process(double t, double baseHz, double syllablesPerSecond, bool sung) {
        const double vibrato = sung ? 1 + 0.012 * std::sin(2 * kPi * 5.2 * t) : 1 + 0.08 * std::sin(2 * kPi * 1.3 * t);
        phase += baseHz * vibrato / fs;
        if (phase >= 1) phase -= 1;
        const double pulse = std::exp(-phase * 18) - 0.06;
        const double syllable = std::fmod(t * syllablesPerSecond, 1.0);
        const double gate = sung ? 0.55 + 0.45 * std::sin(kPi * syllable) : (syllable < 0.7 ? std::sin(kPi * syllable / 0.7) : 0.0);
        const double pause = sung ? 1.0 : (std::fmod(t, 5.0) < 4.2 ? 1.0 : 0.0);
        const double source = pulse + 0.02 * breath.next();
        return (f1.process(source) * 1.0 + f2.process(source) * 0.6 + f3.process(source) * 0.35) * gate * pause;
    }
};

inline double saw(double phase) { return 2.0 * (phase - std::floor(phase + 0.5)); }

}  // namespace detail

inline Stereo pop(double fs, double seconds) {
    using namespace detail;
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    Stereo s{std::vector<float>(n), std::vector<float>(n)};
    Rng snareNoise{3}, hatNoise{5}, pickA{7}, pickB{11};
    Voice voice; voice.prepare(fs);
    Resonator snareBody, hatBand, guitarA, guitarB;
    snareBody.set(fs, 1800, 0.8); hatBand.set(fs, 9000, 1.2); guitarA.set(fs, 1400, 0.7); guitarB.set(fs, 1500, 0.7);
    OnePole bassTone; bassTone.lowpass(fs, 380);
    Reverb verbL, verbR;
    verbL.prepare(fs, {29.7, 37.1, 41.1, 43.7}, {5.0, 1.7}, 0.9);
    verbR.prepare(fs, {31.3, 36.7, 40.3, 45.9}, {4.7, 1.9}, 0.9);
    const double beat = 0.5;   // 120 BPM
    const std::array<double, 4> roots = {55.0, 43.65, 65.41, 49.0};   // A1 F1 C2 G1
    double bassPhase = 0, chordA[3] = {0, 0, 0}, chordB[3] = {0, 0, 0};
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double inBeat = std::fmod(t, beat);
        const int beatIndex = static_cast<int>(t / beat);
        const double root = roots[static_cast<std::size_t>(beatIndex / 4) % 4];
        const double kick = std::sin(2 * kPi * (45 * inBeat + 45 * 0.04 * (1 - std::exp(-inBeat / 0.04)))) * std::exp(-inBeat / 0.15);
        const double snare = (beatIndex % 2 == 1) ? (snareBody.process(snareNoise.next()) * 1.4 + 0.3 * std::sin(2 * kPi * 185 * inBeat)) * std::exp(-inBeat / 0.12) : 0.0;
        const double eighth = std::fmod(t, beat / 2);
        const double hat = hatBand.process(hatNoise.next()) * std::exp(-eighth / 0.035) * 0.5;
        bassPhase += root / fs;
        const double bass = bassTone.process(saw(bassPhase)) * (0.6 + 0.4 * std::exp(-eighth / 0.2));
        const double ratios[3] = {2.0, 2.52, 3.0};
        double gA = 0, gB = 0;
        for (int k = 0; k < 3; ++k) {
            chordA[k] += root * ratios[k] * 1.003 / fs; chordB[k] += root * ratios[k] * 0.997 / fs;
            gA += saw(chordA[k]); gB += saw(chordB[k]);
        }
        const double strum = std::exp(-eighth / 0.18);
        const double guitarL = guitarA.process(gA + 0.2 * pickA.next()) * strum;
        const double guitarR = guitarB.process(gB + 0.2 * pickB.next()) * std::exp(-std::fmod(t + 0.013, beat / 2) / 0.18);
        const double vocal = voice.process(t, root * 4, 4.0, true);
        const double send = vocal * 0.5 + snare * 0.5;
        const double wetL = verbL.process(send), wetR = verbR.process(send);
        const double centre = kick * 0.55 + snare * 0.35 + bass * 0.35 + vocal * 0.3;
        s.left[i] = static_cast<float>(0.3 * (centre + hat * 0.35 + guitarL * 0.7 + wetL * 0.6));
        s.right[i] = static_cast<float>(0.3 * (centre + hat * 0.65 + guitarR * 0.7 + wetR * 0.6));
    }
    return s;
}

inline Stereo ballad(double fs, double seconds) {
    using namespace detail;
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    Stereo s{std::vector<float>(n), std::vector<float>(n)};
    Voice voice; voice.prepare(fs);
    Reverb verbL, verbR;
    verbL.prepare(fs, {47.3, 53.9, 61.1, 67.7}, {7.3, 2.3}, 2.4);
    verbR.prepare(fs, {49.9, 56.1, 59.3, 70.1}, {6.9, 2.9}, 2.4);
    constexpr int kVoices = 6;
    double padPhase[kVoices] = {}, padPan[kVoices] = {-0.9, 0.7, -0.4, 0.95, -0.7, 0.3};
    OnePole padTone[2]; padTone[0].lowpass(fs, 1800); padTone[1].lowpass(fs, 1800);
    const double bar = 60.0 / 70 * 4;
    const std::array<double, 4> chords = {130.81, 110.0, 87.31, 98.0};   // C A F G
    std::vector<double> notePhase(4, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double root = chords[static_cast<std::size_t>(t / bar) % 4];
        const double inBar = std::fmod(t, bar), inBeat = std::fmod(t, bar / 4);
        // Piano: four chord tones, struck each beat, low left to high right.
        const double ratios[4] = {1.0, 1.26, 1.5, 2.0};
        double pianoL = 0, pianoR = 0;
        for (int k = 0; k < 4; ++k) {
            notePhase[static_cast<std::size_t>(k)] += root * ratios[k] / fs;
            const double ph = notePhase[static_cast<std::size_t>(k)];
            const double tone = (std::sin(2 * kPi * ph) + 0.4 * std::sin(4 * kPi * ph) + 0.15 * std::sin(6 * kPi * ph)) * std::exp(-inBeat / 1.2);
            const double pan = -0.6 + 0.4 * k;
            pianoL += tone * std::sqrt(0.5 * (1 - pan));
            pianoR += tone * std::sqrt(0.5 * (1 + pan));
        }
        double padL = 0, padR = 0;
        for (int v = 0; v < kVoices; ++v) {
            padPhase[v] += root * 2 * (1 + 0.004 * std::sin(2 * kPi * (0.13 + 0.07 * v) * t + v)) / fs;
            const double voiceOut = saw(padPhase[v]) * std::min(1.0, inBar / 0.8);
            padL += voiceOut * std::sqrt(0.5 * (1 - padPan[v]));
            padR += voiceOut * std::sqrt(0.5 * (1 + padPan[v]));
        }
        padL = padTone[0].process(padL); padR = padTone[1].process(padR);
        const double vocal = voice.process(t, root * 2, 2.0, true);
        const double send = vocal * 0.6 + (pianoL + pianoR) * 0.2;
        const double wetL = verbL.process(send), wetR = verbR.process(send);
        s.left[i] = static_cast<float>(0.25 * (vocal * 0.5 + pianoL * 0.3 + padL * 0.12 + wetL * 0.6));
        s.right[i] = static_cast<float>(0.25 * (vocal * 0.5 + pianoR * 0.3 + padR * 0.12 + wetR * 0.6));
    }
    return s;
}

inline Stereo electronic(double fs, double seconds) {
    using namespace detail;
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    Stereo s{std::vector<float>(n), std::vector<float>(n)};
    double subPhase = 0, sawL[3] = {}, sawR[3] = {}, fxPhase = 0;
    OnePole toneL, toneR, fxTone; toneL.lowpass(fs, 3000); toneR.lowpass(fs, 3000); fxTone.lowpass(fs, 6000);
    Rng fxNoise{23};
    const double beat = 60.0 / 126;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double inBeat = std::fmod(t, beat);
        const double kick = std::sin(2 * kPi * (50 * inBeat + 60 * 0.03 * (1 - std::exp(-inBeat / 0.03)))) * std::exp(-inBeat / 0.2);
        subPhase += 41.2 / fs;
        const double sub = std::sin(2 * kPi * subPhase) * (1 - std::exp(-inBeat / 0.08));
        double padL = 0, padR = 0;
        const double detune[3] = {-0.012, 0.0, 0.011};
        for (int k = 0; k < 3; ++k) {
            sawL[k] += 220 * (1 + detune[k]) / fs;
            sawR[k] += 220 * (1 - detune[k]) / fs;
            padL += saw(sawL[k]); padR += saw(sawR[k]);
        }
        const double pump = 1 - 0.7 * std::exp(-inBeat / 0.12);
        padL = toneL.process(padL) * pump; padR = toneR.process(padR) * pump;
        fxPhase += 0.25 / fs;
        const double fx = fxTone.process(fxNoise.next()) * (0.5 + 0.5 * std::sin(2 * kPi * fxPhase));
        s.left[i] = static_cast<float>(0.28 * (kick * 0.6 + sub * 0.35 + padL * 0.18 + fx * 0.25));
        s.right[i] = static_cast<float>(0.28 * (kick * 0.6 + sub * 0.35 + padR * 0.18 - fx * 0.25));
    }
    return s;
}

inline Stereo podcast(double fs, double seconds) {
    using namespace detail;
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    Stereo s{std::vector<float>(n), std::vector<float>(n)};
    Voice voice; voice.prepare(fs);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const float v = static_cast<float>(3.0 * voice.process(t, 120, 5.0, false));
        s.left[i] = v;
        s.right[i] = v;
    }
    return s;
}

}  // namespace roomcut::programs

#endif  // ROOMCUT_PROGRAM_SIGNALS_HPP
