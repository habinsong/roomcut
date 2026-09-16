/*
 * RoomSim.hpp — optional virtual room for headphone listening.
 *
 * Headphone playback is anechoic: the signal reaches the ears with no room
 * around it, which is why a mix can stay stuck "inside the head". Adding a
 * small, plausible room restores the reflection cues that externalisation
 * relies on. Two stages, both long-established public techniques:
 *
 *   1. Early reflections — an image-source model of a rectangular room
 *      (Allen & Berkley, JASA 65(4), 1979). Reflection times, levels and
 *      directions come from the room geometry and a Sabine absorption
 *      coefficient, not from a synthetic pattern.
 *   2. Late tail — a feedback delay network (Stautner & Puckette, CMJ 1982;
 *      Jot & Chaigne, AES 90, 1991): eight delay lines, a Householder
 *      feedback matrix, per-line gains that set the decay time and a one-pole
 *      damping filter that shortens the top end the way a real room does.
 *
 * Everything here is computed from those published models; no third-party code
 * or data is used.
 *
 * Safety rules this stage keeps (they are covered by core/tests/test_roomsim.cpp):
 *   - Off is bit-identical: an inactive RoomSim never touches the samples.
 *   - The wet level only moves along a 20 ms ramp, and a room change waits for
 *     the ramp to reach zero before it swaps tables, so switching cannot click.
 *   - The send is high-passed, so the room never muddies the bass.
 *   - Feedback states are denormal-flushed and the decay is strictly < 1.
 *   - prepare() owns every allocation; processFrame() allocates nothing.
 *
 * It runs after the chain's dry path and before the limiter, so the extra
 * reverb energy still passes the brickwall safety stage.
 *
 * Speakers get a different room, not the same one turned down. A listener on
 * speakers already sits in a real room, and its early reflections are real: a
 * second set of synthetic ones lands in the same few milliseconds and combs
 * against them. So the speaker profile drops the reflection stage entirely and
 * plays only the late, diffuse field — quieter, later and darker, so it reads
 * as the space around the speakers rather than a second room in front of them.
 */
#ifndef ROOMCUT_ROOM_SIM_HPP
#define ROOMCUT_ROOM_SIM_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace roomcut {

class RoomSim {
public:
    // Room ids. 0 = off; 1..3 index kRooms below.
    static constexpr int kOff = 0;
    static constexpr int kStudio = 1;
    static constexpr int kLiving = 2;
    static constexpr int kHall = 3;

    static constexpr std::size_t kNumRooms = 3;
    // Reflection count is a sound-quality choice, not just a cost one: a sparse
    // set combs a steady tone (a handful of fixed delays adding in and out of
    // phase), while a denser one averages out the way a real room does.
    static constexpr std::size_t kMaxTaps = 24;   // early reflections kept per side
    static constexpr std::size_t kLines = 8;      // FDN delay lines

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        // 20 ms wet ramp: long enough to be inaudible, short enough that a room
        // change feels immediate.
        rampStep_ = 1.0 / std::max(1.0, fs_ * 0.020);
        // First-order high-pass on the send (~180 Hz). Bass carries most of the
        // energy and nothing useful for externalisation; keeping it out of the
        // room is what stops the "muddy" complaint.
        sendHpA_ = 1.0 / (1.0 + 2.0 * kPi * kSendHighpassHz / fs_);

        predelayLen_ = static_cast<std::size_t>(std::lround(fs_ * kMaxPredelaySec)) + 2;
        erLen_ = static_cast<std::size_t>(std::lround(fs_ * kMaxErSec)) + 2;
        predelay_.assign(predelayLen_, 0.0);
        erL_.assign(erLen_, 0.0);
        erR_.assign(erLen_, 0.0);

        std::size_t longestLine = 0;
        for (std::size_t r = 0; r < kNumRooms; ++r)
            for (std::size_t i = 0; i < kLines; ++i)
                longestLine = std::max(longestLine,
                    static_cast<std::size_t>(std::lround(fs_ * kRooms[r].lineMs[i] * 0.001)));
        lineLen_ = longestLine + 2;
        for (std::size_t i = 0; i < kLines; ++i) lines_[i].assign(lineLen_, 0.0);

        buildRooms();
        activeRoom_ = -1;
        requestedRoom_ = -1;
        reset();
    }

    // type: 0 = off, 1 = Studio, 2 = Living Room, 3 = Hall.
    // amount: 0..100; 50 is the room's reference level, 100 is twice that
    // amplitude. Anything outside the table turns the stage off.
    // headphone: false selects the speaker profile (late field only, see above).
    void setParams(int type, double amount, bool headphone = true) {
        const int room = (type >= kStudio && type <= kHall) ? type : kOff;
        amount_ = std::min(200.0, std::max(0.0, amount));
        // Amount 0 is the same thing as off: stop feeding the room so it costs
        // nothing rather than running a silent tail.
        requestedRoom_ = (room == kOff || amount_ <= 0.0) ? -1 : room - 1;
        requestedHeadphone_ = headphone;
        const auto& spec = rooms_[static_cast<std::size_t>(std::max(requestedRoom_, 0))];
        wetTarget_ = requestedRoom_ < 0
            ? 0.0
            : (headphone ? spec.wetLin : spec.speakerWetLin) * amountScale(spec.amountCeiling);
    }

    // Amount 50 is the room's reference level and stays exactly that. Below it
    // the slider is linear down to silence; above it the curve flattens toward
    // the room's ceiling, so the top of the slider adds space instead of comb.
    double amountScale(double ceiling) const {
        if (amount_ <= 50.0) return amount_ / 50.0;
        const double over = std::min(1.0, (amount_ - 50.0) / 50.0);
        return 1.0 + (ceiling - 1.0) * std::pow(over, 0.7);
    }

    void reset() {
        std::fill(predelay_.begin(), predelay_.end(), 0.0);
        std::fill(erL_.begin(), erL_.end(), 0.0);
        std::fill(erR_.begin(), erR_.end(), 0.0);
        for (std::size_t i = 0; i < kLines; ++i) {
            std::fill(lines_[i].begin(), lines_[i].end(), 0.0);
            lineWrite_[i] = 0;
            damp_[i] = 0.0;
        }
        predelayWrite_ = 0;
        erWrite_ = 0;
        erLpL_ = erLpR_ = 0.0;
        sendHpXL_ = sendHpYL_ = sendHpXR_ = sendHpYR_ = 0.0;
        // Ramp state is deliberately preserved: reset() is also called on a
        // sample-rate change, where the caller re-applies the parameters.
        wetCur_ = 0.0;
    }

    inline void processFrame(float* frame, std::size_t channels) {
        // Fully idle: leave the samples untouched so "off" is bit-identical.
        if (channels < 2) return;
        if (activeRoom_ < 0 && wetCur_ <= 0.0) {
            if (requestedRoom_ < 0) return;
            activeHeadphone_ = requestedHeadphone_;
            activeRoom_ = requestedRoom_;
            loadRoom(static_cast<std::size_t>(activeRoom_));
        }

        // A room change fades the current room out first; the swap happens at
        // silence, so no tail is ever cut mid-level. Switching between the
        // headphone and speaker profiles is the same kind of change.
        const bool sameProfile = requestedRoom_ == activeRoom_ && requestedHeadphone_ == activeHeadphone_;
        const double target = sameProfile ? wetTarget_ : 0.0;
        // A fixed linear step (full scale in 20 ms) always reaches the target
        // exactly — an exponential approach would never hit zero, so a room
        // swap could never complete.
        if (wetCur_ < target)      wetCur_ = std::min(target, wetCur_ + rampStep_);
        else if (wetCur_ > target) wetCur_ = std::max(target, wetCur_ - rampStep_);
        if (wetCur_ <= 0.0 && !sameProfile) {
            wetCur_ = 0.0;
            if (requestedRoom_ < 0) {
                activeRoom_ = -1;
                silence();
                return;
            }
            activeHeadphone_ = requestedHeadphone_;
            activeRoom_ = requestedRoom_;
            loadRoom(static_cast<std::size_t>(activeRoom_));
            silence();
        }

        const double left = frame[0];
        const double right = frame[1];

        // Send: high-passed, so only the band that carries directional and
        // envelopment cues drives the room.
        const double hpL = sendHpA_ * (sendHpYL_ + left - sendHpXL_);
        sendHpXL_ = left; sendHpYL_ = hpL;
        const double hpR = sendHpA_ * (sendHpYR_ + right - sendHpXR_);
        sendHpXR_ = right; sendHpYR_ = hpR;

        // Early reflections. The tap table is computed for the left virtual
        // speaker; the right speaker is its mirror image, so the same taps are
        // read with the ears swapped. The room stays left/right symmetric.
        erL_[erWrite_] = hpL;
        erR_[erWrite_] = hpR;
        double erOutL = 0.0, erOutR = 0.0;
        for (std::size_t t = 0; t < tapCount_ && erWeight_ > 0.0; ++t) {
            const Tap& tap = taps_[t];
            erOutL += tap.gainNear * erL_[erRead(tap.delayNear)] + tap.gainFar * erR_[erRead(tap.delayFar)];
            erOutR += tap.gainFar * erL_[erRead(tap.delayFar)] + tap.gainNear * erR_[erRead(tap.delayNear)];
        }
        erWrite_ = erWrite_ + 1 < erLen_ ? erWrite_ + 1 : 0;
        // Walls absorb the top end on every bounce. One filter on the bus stands
        // in for that (cheaper than filtering each reflection) and it also takes
        // the edge off the comb the discrete taps would otherwise leave.
        erLpL_ += dampA_ * (erOutL - erLpL_);
        erLpR_ += dampA_ * (erOutR - erLpR_);
        erOutL = erLpL_;
        erOutR = erLpR_;

        // Late tail. One mono send feeds the network; the lines are read out in
        // two interleaved groups so the tail is decorrelated between the ears.
        predelay_[predelayWrite_] = (hpL + hpR) * 0.5;
        const std::size_t preRead = predelayWrite_ + predelayLen_ - predelaySamples_ >= predelayLen_
            ? predelayWrite_ + predelayLen_ - predelaySamples_ - predelayLen_
            : predelayWrite_ + predelayLen_ - predelaySamples_;
        const double send = predelay_[preRead];
        predelayWrite_ = predelayWrite_ + 1 < predelayLen_ ? predelayWrite_ + 1 : 0;

        double read[kLines];
        double sum = 0.0;
        for (std::size_t i = 0; i < kLines; ++i) {
            const std::size_t idx = lineWrite_[i] + lineLen_ - lineSamples_[i] >= lineLen_
                ? lineWrite_[i] + lineLen_ - lineSamples_[i] - lineLen_
                : lineWrite_[i] + lineLen_ - lineSamples_[i];
            double v = lines_[i][idx] * lineGain_[i];
            damp_[i] += dampA_ * (v - damp_[i]);          // one-pole HF damping
            v = flush(damp_[i]);
            read[i] = v;
            sum += v;
        }
        // Householder feedback matrix: y_i = (2/N)·Σx − x_i. Unitary, so the
        // decay is set purely by the per-line gains and the network cannot
        // build up energy on its own.
        const double mixed = sum * (2.0 / static_cast<double>(kLines));
        double lateL = 0.0, lateR = 0.0;
        for (std::size_t i = 0; i < kLines; ++i) {
            const double fb = mixed - read[i];
            const double in = (i % 2 == 0 ? send : -send) * kLineInScale;
            lines_[i][lineWrite_[i]] = flush(fb + in);
            lineWrite_[i] = lineWrite_[i] + 1 < lineLen_ ? lineWrite_[i] + 1 : 0;
            if (i % 2 == 0) lateL += read[i]; else lateR += read[i];
        }
        lateL *= kLineOutScale;
        lateR *= kLineOutScale;

        const double wet = wetCur_;
        const double outL = left + wet * (erWeight_ * erOutL + lateWeight_ * lateL);
        const double outR = right + wet * (erWeight_ * erOutR + lateWeight_ * lateR);
        frame[0] = static_cast<float>(outL);
        frame[1] = static_cast<float>(outR);
    }

    // True while the stage is audible (used by tests and diagnostics).
    bool active() const { return activeRoom_ >= 0 || wetCur_ > 0.0; }
    double wetGain() const { return wetCur_; }

private:
    struct Tap {
        std::size_t delayNear = 0;   // ipsilateral ear delay, samples
        std::size_t delayFar = 0;    // contralateral ear (adds the ITD)
        double gainNear = 0.0;
        double gainFar = 0.0;
    };

    struct RoomSpec {
        double lx, ly, lz;        // room dimensions, m
        double rt60;              // broadband decay time, s
        double srcDist;           // virtual speaker distance, m
        double predelayMs;
        double wetDb;             // reverb-to-direct level at amount = 50
        double dampHz;            // one-pole damping cutoff in the tail
        double lateFrac;          // share of the wet energy carried by the tail
        // Speaker profile (late field only): its own level, how far behind the
        // direct sound it starts, and how much darker it is. Kept per room so
        // the three rooms are as distinct on speakers as they are on headphones.
        double speakerWetScale;
        double speakerPredelayMs;
        double speakerDampScale;
        // How far past the reference level the Amount slider may push this room.
        // Past a point more wet stops sounding like more space and starts
        // sounding like comb filtering against the dry signal — measured as
        // third-octave ripple, and the bigger the room the sooner it happens.
        double amountCeiling;
        double lineMs[kLines];    // FDN delay lengths
    };

    // Three rooms that have to sound like three different places, not three
    // settings of one. Every cue a listener uses to size a room is pulled apart
    // between them: how long the first reflection takes to arrive (predelay),
    // how long the tail runs, how much of the sound is room rather than source,
    // how fast the top end dies, and how much of the energy is late rather than
    // early. Studio stays the conservative default — the smallest simulated room
    // is the least likely to fight the listener's real one.
    static constexpr RoomSpec kRooms[kNumRooms] = {
        // Studio — small, tight, close. Mostly early reflections.
        {4.5, 3.6, 2.8, 0.22, 1.8, 3.0, -15.0, 7000.0, 0.35,
         0.60, 6.0, 0.85, 1.80,
         {19.0, 23.0, 29.0, 31.0, 37.0, 41.0, 43.0, 47.0}},
        // Living Room — clearly bigger: tail 2.5x longer, audibly wetter, darker.
        {7.0, 5.5, 2.8, 0.55, 2.6, 12.0, -12.5, 4500.0, 0.60,
         0.85, 14.0, 0.70, 1.50,
         {29.0, 37.0, 43.0, 53.0, 59.0, 67.0, 71.0, 79.0}},
        // Hall — a big space where the tail dominates and arrives well behind.
        {24.0, 16.0, 11.0, 1.70, 6.0, 26.0, -11.0, 3000.0, 0.78,
         1.15, 28.0, 0.55, 1.30,
         {53.0, 67.0, 79.0, 89.0, 101.0, 113.0, 127.0, 139.0}},
    };

    struct RoomTables {
        std::array<Tap, kMaxTaps> taps{};
        std::size_t tapCount = 0;
        std::array<std::size_t, kLines> lineSamples{};
        std::array<double, kLines> lineGain{};
        std::size_t predelaySamples = 1;
        std::size_t speakerPredelaySamples = 1;
        double dampA = 0.5;
        double speakerDampA = 0.5;
        double speakerWetLin = 0.0;
        double erWeight = 0.7071067811865476;   // energy split, er² + late² = 1
        double lateWeight = 0.7071067811865476;
        double lateNorm = 1.0;                  // makes the tail unit-energy
        double amountCeiling = 1.5;             // how far past reference Amount may go
        double wetLin = 0.0;                    // reverb-to-direct amplitude
    };

    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kSpeedOfSound = 343.0;       // m/s, 20 °C
    static constexpr double kHeadRadius = 0.0875;        // m (Woodworth ITD)
    static constexpr double kSendHighpassHz = 180.0;
    static constexpr double kMaxPredelaySec = 0.040;   // 26 ms Hall + the speaker offset
    static constexpr double kMaxErSec = 0.100;           // longest reflection kept
    static constexpr double kLineInScale = 0.35355339059327373;  // 1/sqrt(8)
    static constexpr double kLineOutScale = 0.5;                 // 1/sqrt(4)

    static double flush(double v) { return std::fabs(v) < 1.0e-25 ? 0.0 : v; }

    std::size_t erRead(std::size_t delay) const {
        const std::size_t back = erWrite_ + erLen_ - delay;
        return back >= erLen_ ? back - erLen_ : back;
    }

    // Woodworth's frequency-independent ITD for a sphere: the extra path the
    // far ear adds for a source at azimuth |az|.
    static double itdSeconds(double azRad) {
        const double a = std::min(std::fabs(azRad), kPi * 0.5);
        return (kHeadRadius / kSpeedOfSound) * (a + std::sin(a));
    }

    void buildRooms() {
        for (std::size_t r = 0; r < kNumRooms; ++r) buildRoom(r);
    }

    void buildRoom(std::size_t r) {
        const RoomSpec& s = kRooms[r];
        RoomTables& t = rooms_[r];

        // Sabine: RT60 = 0.161·V / (S·α) → the absorption that produces the
        // room's decay time, and with it the wall reflection coefficient.
        const double volume = s.lx * s.ly * s.lz;
        const double surface = 2.0 * (s.lx * s.ly + s.lx * s.lz + s.ly * s.lz);
        const double alpha = std::min(0.9, std::max(0.02, 0.161 * volume / (surface * s.rt60)));
        const double beta = std::sqrt(1.0 - alpha);

        // Listener a third of the way into the room, virtual speaker at −30°.
        const double listener[3] = {s.lx * 0.5, s.ly * 0.35, 1.2};
        const double source[3] = {
            listener[0] - s.srcDist * 0.5,              // sin(30°)
            listener[1] + s.srcDist * 0.8660254037844387, // cos(30°)
            listener[2]};

        struct Candidate { double delaySec; double gain; double az; };
        Candidate found[512];
        std::size_t count = 0;
        const double maxDelay = kMaxErSec;

        // Image sources up to third order. Mirroring along an axis flips the
        // coordinate and multiplies the amplitude by the wall coefficient.
        for (int kx = -3; kx <= 3; ++kx) {
            for (int ky = -3; ky <= 3; ++ky) {
                for (int kz = -3; kz <= 3; ++kz) {
                    const int order = std::abs(kx) + std::abs(ky) + std::abs(kz);
                    if (order == 0 || order > 3) continue;
                    const double img[3] = {
                        mirror(source[0], s.lx, kx),
                        mirror(source[1], s.ly, ky),
                        mirror(source[2], s.lz, kz)};
                    const double dx = img[0] - listener[0];
                    const double dy = img[1] - listener[1];
                    const double dz = img[2] - listener[2];
                    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist < 1.0e-3) continue;
                    const double delaySec = (dist - s.srcDist) / kSpeedOfSound;
                    if (delaySec <= 0.0 || delaySec > maxDelay) continue;
                    // Level relative to the direct sound: spherical spreading
                    // plus one wall coefficient per reflection.
                    const double gain = (s.srcDist / dist) * std::pow(beta, order);
                    if (gain < 0.02) continue;
                    if (count < 512) {
                        found[count].delaySec = delaySec;
                        found[count].gain = gain;
                        found[count].az = std::atan2(dx, dy);  // 0 = front, + = right
                        ++count;
                    }
                }
            }
        }

        // Keep the strongest reflections; they are the ones that carry the cue.
        std::sort(found, found + count,
                  [](const Candidate& a, const Candidate& b) { return a.gain > b.gain; });
        t.tapCount = std::min(count, kMaxTaps);
        for (std::size_t i = 0; i < t.tapCount; ++i) {
            const Candidate& c = found[i];
            // Constant-power lateral placement; the far ear also gets the ITD.
            const double pan = std::sin(c.az);
            const double gNear = std::sqrt(0.5 * (1.0 + std::fabs(pan)));
            const double gFar = std::sqrt(0.5 * (1.0 - std::fabs(pan)));
            const double base = std::lround(c.delaySec * fs_);
            const double itd = std::lround(itdSeconds(c.az) * fs_);
            const std::size_t dNear = clampDelay(static_cast<std::size_t>(base));
            const std::size_t dFar = clampDelay(static_cast<std::size_t>(base + itd));
            Tap tap;
            // az > 0 means the reflection arrives from the right, so for the
            // LEFT input the near ear is the right one. The mirrored read in
            // processFrame keeps the whole set symmetric.
            tap.delayNear = pan >= 0.0 ? dFar : dNear;
            tap.delayFar = pan >= 0.0 ? dNear : dFar;
            tap.gainNear = (pan >= 0.0 ? gFar : gNear) * c.gain;
            tap.gainFar = (pan >= 0.0 ? gNear : gFar) * c.gain;
            t.taps[i] = tap;
        }

        // Normalise the reflection set to unit energy. The taps sit at distinct
        // delays, so their contributions are uncorrelated and the energies add;
        // after this the wet level below means exactly "reverb relative to the
        // direct sound", independent of how many reflections a room produced.
        double tapEnergy = 0.0;
        for (std::size_t i = 0; i < t.tapCount; ++i)
            tapEnergy += t.taps[i].gainNear * t.taps[i].gainNear + t.taps[i].gainFar * t.taps[i].gainFar;
        const double erNorm = tapEnergy > 0.0 ? 1.0 / std::sqrt(tapEnergy) : 0.0;
        for (std::size_t i = 0; i < t.tapCount; ++i) {
            t.taps[i].gainNear *= erNorm;
            t.taps[i].gainFar *= erNorm;
        }

        for (std::size_t i = 0; i < kLines; ++i) {
            const std::size_t n = std::max<std::size_t>(
                1, static_cast<std::size_t>(std::lround(fs_ * s.lineMs[i] * 0.001)));
            t.lineSamples[i] = n;
            // Per-line gain for the target decay: −60 dB after rt60 seconds.
            const double seconds = static_cast<double>(n) / fs_;
            t.lineGain[i] = std::min(0.9999, std::pow(10.0, -3.0 * seconds / s.rt60));
        }
        t.predelaySamples = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::lround(fs_ * s.predelayMs * 0.001)));
        t.dampA = 1.0 - std::exp(-2.0 * kPi * std::min(s.dampHz, fs_ * 0.45) / fs_);
        t.speakerPredelaySamples = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::lround(fs_ * s.speakerPredelayMs * 0.001)));
        t.speakerDampA = 1.0 - std::exp(
            -2.0 * kPi * std::min(s.dampHz * s.speakerDampScale, fs_ * 0.45) / fs_);
        t.erWeight = std::sqrt(1.0 - s.lateFrac);
        t.lateWeight = std::sqrt(s.lateFrac);
        t.wetLin = std::pow(10.0, s.wetDb / 20.0);
        t.speakerWetLin = t.wetLin * s.speakerWetScale;   // after wetLin, not before
        t.amountCeiling = s.amountCeiling;
        // The tail's own gain depends on the line lengths and the decay time,
        // so it is measured rather than guessed: run the network's impulse
        // response once here (prepare time, never on the render thread) and
        // scale it to unit energy.
        const double lateEnergy = measureLateEnergy(t, s.rt60);
        t.lateNorm = lateEnergy > 0.0 ? 1.0 / std::sqrt(lateEnergy) : 0.0;
    }

    // Offline probe of the FDN, using the same arithmetic as processFrame.
    // Runs on the prepare path and leaves the delay lines zeroed.
    double measureLateEnergy(const RoomTables& t, double rt60) {
        for (std::size_t i = 0; i < kLines; ++i) std::fill(lines_[i].begin(), lines_[i].end(), 0.0);
        std::array<double, kLines> damp{};
        std::array<std::size_t, kLines> write{};
        const std::size_t n = static_cast<std::size_t>(fs_ * (rt60 * 1.5 + 0.1));
        double total = 0.0;
        for (std::size_t s = 0; s < n; ++s) {
            const double send = (s == 0) ? 1.0 : 0.0;
            double read[kLines];
            double sum = 0.0;
            for (std::size_t i = 0; i < kLines; ++i) {
                const std::size_t idx = write[i] + lineLen_ - t.lineSamples[i] >= lineLen_
                    ? write[i] + lineLen_ - t.lineSamples[i] - lineLen_
                    : write[i] + lineLen_ - t.lineSamples[i];
                const double v = lines_[i][idx] * t.lineGain[i];
                damp[i] += t.dampA * (v - damp[i]);
                read[i] = flush(damp[i]);
                sum += read[i];
            }
            const double mixed = sum * (2.0 / static_cast<double>(kLines));
            double outL = 0.0, outR = 0.0;
            for (std::size_t i = 0; i < kLines; ++i) {
                const double in = (i % 2 == 0 ? send : -send) * kLineInScale;
                lines_[i][write[i]] = flush(mixed - read[i] + in);
                write[i] = write[i] + 1 < lineLen_ ? write[i] + 1 : 0;
                if (i % 2 == 0) outL += read[i]; else outR += read[i];
            }
            outL *= kLineOutScale;
            outR *= kLineOutScale;
            total += (outL * outL + outR * outR) * 0.5;
        }
        for (std::size_t i = 0; i < kLines; ++i) std::fill(lines_[i].begin(), lines_[i].end(), 0.0);
        return total;
    }

    static double mirror(double coord, double length, int k) {
        // Even k: the source translated by whole rooms. Odd k: its reflection.
        return (k % 2 == 0) ? (k * length + coord) : ((k + 1) * length - coord);
    }

    std::size_t clampDelay(std::size_t d) const {
        return std::min(d, erLen_ - 1);
    }

    void loadRoom(std::size_t r) {
        const RoomTables& t = rooms_[r];
        taps_ = t.taps;
        tapCount_ = t.tapCount;
        lineSamples_ = t.lineSamples;
        lineGain_ = t.lineGain;
        dampA_ = t.dampA;
        if (activeHeadphone_) {
            predelaySamples_ = std::min(t.predelaySamples, predelayLen_ - 1);
            erWeight_ = t.erWeight;
            lateWeight_ = t.lateWeight * t.lateNorm;
        } else {
            // Speakers: no synthetic early reflections (the real room owns those),
            // the whole wet signal is the late field, it starts later so it sits
            // behind the direct sound, and it is darker.
            predelaySamples_ = std::min(t.speakerPredelaySamples, predelayLen_ - 1);
            erWeight_ = 0.0;
            lateWeight_ = t.lateNorm;
            dampA_ = t.speakerDampA;
        }
    }

    // Clear the audible state without touching the ramp (used at a room swap,
    // which only happens while the wet level is exactly zero).
    void silence() {
        std::fill(predelay_.begin(), predelay_.end(), 0.0);
        std::fill(erL_.begin(), erL_.end(), 0.0);
        std::fill(erR_.begin(), erR_.end(), 0.0);
        for (std::size_t i = 0; i < kLines; ++i) {
            std::fill(lines_[i].begin(), lines_[i].end(), 0.0);
            damp_[i] = 0.0;
        }
        erLpL_ = erLpR_ = 0.0;
    }

    double fs_ = 48000.0;
    double rampStep_ = 1.0;
    double sendHpA_ = 0.99;
    double sendHpXL_ = 0.0, sendHpYL_ = 0.0, sendHpXR_ = 0.0, sendHpYR_ = 0.0;

    std::array<RoomTables, kNumRooms> rooms_{};
    int activeRoom_ = -1;       // index into rooms_, −1 = silent
    int requestedRoom_ = -1;
    bool activeHeadphone_ = true;
    bool requestedHeadphone_ = true;
    double amount_ = 50.0;
    double wetTarget_ = 0.0;
    double wetCur_ = 0.0;

    std::array<Tap, kMaxTaps> taps_{};
    std::size_t tapCount_ = 0;
    std::array<std::size_t, kLines> lineSamples_{};
    std::array<double, kLines> lineGain_{};
    std::size_t predelaySamples_ = 1;
    double dampA_ = 0.5;
    double erWeight_ = 0.0;
    double lateWeight_ = 0.0;

    std::vector<double> predelay_;
    std::vector<double> erL_;
    std::vector<double> erR_;
    std::array<std::vector<double>, kLines> lines_{};
    std::array<double, kLines> damp_{};
    std::array<std::size_t, kLines> lineWrite_{};
    std::size_t predelayLen_ = 0, erLen_ = 0, lineLen_ = 0;
    std::size_t predelayWrite_ = 0, erWrite_ = 0;
    double erLpL_ = 0.0, erLpR_ = 0.0;   // reflection-bus absorption
};

} // namespace roomcut

#endif // ROOMCUT_ROOM_SIM_HPP
