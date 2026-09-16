/*
 * SpeakerColour.hpp — undoing the colour a virtual loudspeaker adds at a given
 * angle.
 *
 * A VirtualSpeaker is not equally bright at every angle, and the difference is
 * not a level — it is a tilt. Measured at 48 kHz, one speaker's summed binaural
 * response:
 *
 *        angle     low (50-250 Hz)   high (4-14 kHz)   tilt
 *          0         +2.96 dB          -1.50 dB      -4.46 dB
 *         30         +2.99             +1.34         -1.64
 *         90         +3.08             +5.87         +2.79
 *        110         +3.08             +5.85         +2.78
 *        135         +3.05             +4.88         +1.83
 *
 * The bottom is the same everywhere — both ears hear the whole signal, so the
 * power doubles — but the top varies by 7.4 dB between front and side: the near
 * ear of a lateral source sits inside the shadow filter's lift, while a frontal
 * source reaches both ears from behind their 100-degree axis and loses its top.
 *
 * A stereo pair never had to care, because its two speakers move together and
 * their changes cancel. An upmix does: it moves material between front and
 * surround as the programme changes, so without this the same voice would change
 * colour depending on how correlated the mix happened to be. Worse, the centre
 * is the dullest position of all, and the centre channel is the dialogue.
 *
 * The correction is a first-order shelf split at 1 kHz, applied to the speaker's
 * MONO FEED. That matters: applied to the two ear signals it would flatten the
 * difference between them, which is the direction cue itself. Applied to the
 * feed, only the colouring common to both ears goes away. This is what a
 * diffuse-field-equalised HRTF set does, for the same reason.
 */
#ifndef ROOMCUT_SPEAKER_COLOUR_HPP
#define ROOMCUT_SPEAKER_COLOUR_HPP

#include <cmath>

namespace roomcut {

class SpeakerColour {
public:
    void prepare(double fs) {
        coefficient_ = 1.0 - std::exp(-2.0 * kPi * kSplitHz / (fs > 0.0 ? fs : 48000.0));
        reset();
    }

    void reset() { state_ = 0.0; }

    // Flat: what a speaker outside an upmix gets, and bit-identical to no filter.
    void setFlat() {
        low_ = 1.0;
        high_ = 1.0;
    }

    void setAngle(double degrees) {
        double lowDb = 0.0, highDb = 0.0;
        response(degrees, lowDb, highDb);
        low_ = std::pow(10.0, -lowDb / 20.0);
        high_ = std::pow(10.0, -highDb / 20.0);
    }

    inline double process(double x) {
        state_ += coefficient_ * (x - state_);
        return low_ * state_ + high_ * (x - state_);
    }

    // The measured table, interpolated. Exposed so the tests can check the
    // correction against the numbers it claims to be undoing.
    static void response(double degrees, double& lowDb, double& highDb) {
        static constexpr double kAngle[5] = {0.0, 30.0, 90.0, 110.0, 135.0};
        static constexpr double kLowDb[5] = {2.96, 2.99, 3.08, 3.08, 3.05};
        static constexpr double kHighDb[5] = {-1.50, 1.34, 5.87, 5.85, 4.88};
        const double a = std::fabs(degrees);
        if (a <= kAngle[0]) { lowDb = kLowDb[0]; highDb = kHighDb[0]; return; }
        for (int i = 1; i < 5; ++i) {
            if (a <= kAngle[i]) {
                const double f = (a - kAngle[i - 1]) / (kAngle[i] - kAngle[i - 1]);
                lowDb = kLowDb[i - 1] + f * (kLowDb[i] - kLowDb[i - 1]);
                highDb = kHighDb[i - 1] + f * (kHighDb[i] - kHighDb[i - 1]);
                return;
            }
        }
        lowDb = kLowDb[4];
        highDb = kHighDb[4];
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    // Where the shadow filter's shape turns over.
    static constexpr double kSplitHz = 1000.0;

    double coefficient_ = 0.12, state_ = 0.0, low_ = 1.0, high_ = 1.0;
};

} // namespace roomcut

#endif // ROOMCUT_SPEAKER_COLOUR_HPP
