// tune — choose every value of a Space preset by measurement.
//
// Build (from the repository root):
//   clang++ -std=c++17 -O2 -DNDEBUG -Icore -Icore/dsp -Icore/tests -Iengine/include \
//     scripts/space-presets/tune.cpp engine/src/SpatialMixerBedRenderer.cpp \
//     -framework AudioToolbox -framework CoreFoundation -o /tmp/tune
// Run (audio from fetch-audio.py; one process per preset parallelises well):
//   /tmp/tune <audio dir> <preset id> [<preset id> ...]
// Each preset prints a RESULT line with its values and the measurements they
// were chosen on; gen-swift.py turns the RESULT lines into SpacePresetLibrary.
//
// Each preset states a target; this renders real recordings through the product
// DSPChain (AUSpatialMixer attached) and picks the values that get closest to
// the target while holding the limits below. Nothing is picked by ear.
//
// Targets:
//   sp_match   speakers: interaural correlation at the ears (IACCa of 500 Hz,
//              1 kHz and 2 kHz octaves, through AUSpatialMixer's HRTF) with the
//              speakers close at +-15 deg — desk and computer speakers — as near
//              as possible to what the unprocessed recording gives from a
//              standard +-30 deg stereo pair, the layout mixes are made for.
//   hp_match   headphones: IACCa of the headphone output as close as possible to
//              what the same recording gives at the ears over speakers at +-30.
//   speech     dialogue-to-background ratio (300 Hz - 4 kHz energy of speech over
//              that of music, each rendered alone) as high as possible.
//   movie      speech, but the width target may not get worse than the scene
//              without the stage settings.
// Limits, every programme of the preset. What the stage settings add to the
// scene (surround and room): level within +-1 dB, peak up by at most 1 dB,
// 1/3-octave colour at most 3 dB (speech presets: 2 dB on speech), and on
// speakers the mono sum down by at most 1 dB. A scene that is searched (genre
// presets) also has to hold, against Reference: level within +-1.5 dB, peak
// before the output limiter at most +1 dBFS on a master peaking at -1 dBFS (the
// limiter takes at most 1 dB off), colour at most 4 dB, mono sum down at most
// 1 dB. If nothing holds the limits, nothing moves.
#include "measure.hpp"

struct Scene { bool hp = false; int surround = 0; int room = 0; double amount = 50; double cw = 100, depth = 50; };
struct Stage { double width = 0, center = 0, damping = 0, cross = 0; };

static ChainParams paramsFor(const Scene& s, const Stage& g) {
    ChainParams c = ChainParams::flat();
    const bool amb = s.surround == 1;
    c.spatialMode = s.hp ? (amb ? 2 : 1) : (amb ? 3 : 0);
    c.surroundType = s.surround >= 3 ? 3 : (s.surround == 2 ? 2 : 0);
    c.roomType = s.room; c.roomAmount = s.amount;
    c.centerWidth = s.cw; c.surroundDepth = s.depth; c.bedRenderer = 0;
    c.spatialWidth = g.width; c.centerFocus = g.center; c.roomReduce = g.damping; c.crossfeed = g.cross;
    return c;
}

static Stereo render(const Stereo& in, const ChainParams& p) {
    SpatialMixerBedRenderer renderer; std::string error;
    if (!renderer.prepare(kFs, error)) { std::fprintf(stderr, "AU: %s\n", error.c_str()); std::exit(1); }
    DSPChain chain;
    chain.attachBedRenderer(&renderer);
    chain.prepare(kFs, 2);
    chain.setParams(p);
    chain.reset();
    Stereo out{std::vector<float>(in.l.size()), std::vector<float>(in.l.size())};
    std::vector<float> block(1024);
    for (size_t i = 0; i < in.l.size(); i += 512) {
        const size_t k = std::min<size_t>(512, in.l.size() - i);
        for (size_t j = 0; j < k; ++j) { block[2 * j] = in.l[i + j]; block[2 * j + 1] = in.r[i + j]; }
        chain.processInterleaved(block.data(), k);
        for (size_t j = 0; j < k; ++j) { out.l[i + j] = block[2 * j]; out.r[i + j] = block[2 * j + 1]; }
    }
    return out;
}

static Stereo loadAll(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "missing %s\n", path.c_str()); std::exit(1); }
    std::vector<unsigned char> b; unsigned char buf[1 << 16]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    Stereo s;
    for (size_t p = 12; p + 8 <= b.size();) {
        uint32_t size; std::memcpy(&size, &b[p + 4], 4);
        if (!std::memcmp(&b[p], "data", 4)) {
            const size_t frames = size / 8;
            s.l.resize(frames); s.r.resize(frames);
            for (size_t i = 0; i < frames; ++i) { std::memcpy(&s.l[i], &b[p + 8 + 8 * i], 4); std::memcpy(&s.r[i], &b[p + 12 + 8 * i], 4); }
            break;
        }
        p += 8 + size + (size & 1);
    }
    return s;
}

// The loudest stretch of the track (1 s steps), scaled so its peak is -7 dBFS:
// 6 dB under a master peaking at -1 dBFS, so the output limiter never acts and
// the peak it would have had to catch can be read straight off (+6 dB).
static Stereo excerpt(const std::string& path, double seconds = 10) {
    const Stereo all = loadAll(path);
    const size_t len = (size_t)(seconds * kFs), step = (size_t)kFs;
    size_t best = 0; double bestE = -1;
    for (size_t at = 0; at + len <= all.l.size(); at += step) {
        double e = 0;
        for (size_t i = at; i < at + len; i += 4) e += (double)all.l[i] * all.l[i] + (double)all.r[i] * all.r[i];
        if (e > bestE) { bestE = e; best = at; }
    }
    Stereo s{std::vector<float>(all.l.begin() + best, all.l.begin() + best + len), std::vector<float>(all.r.begin() + best, all.r.begin() + best + len)};
    float peak = 1e-9f;
    for (size_t i = 0; i < len; ++i) peak = std::max({peak, std::abs(s.l[i]), std::abs(s.r[i])});
    const float g = std::pow(10.0f, -7.0f / 20) / peak;
    for (size_t i = 0; i < len; ++i) { s.l[i] *= g; s.r[i] *= g; }
    return s;
}

static double peakDb(const Stereo& x, double skip) {
    double p = 0; for (size_t i = (size_t)(skip * kFs); i < x.l.size(); ++i) p = std::max({p, (double)std::abs(x.l[i]), (double)std::abs(x.r[i])});
    return 20 * std::log10(std::max(p, 1e-9));
}

// One FFT per channel, then everything from it: 1/3-octave energies (colour),
// the 300 Hz - 4 kHz energy (dialogue ratio) and the octave-band IACC at
// 500 Hz, 1 kHz and 2 kHz (inverse FFT of each band).
struct Analysis { std::vector<double> third; double speech = 0; double iaccA = 0; };

static constexpr double kSkip = 2, kSpan = 5.5;

static Analysis analyse(const Stereo& x, bool wantIacc = true) {
    const size_t from = (size_t)(kSkip * kFs), len = std::min(x.l.size() - from, (size_t)(kSpan * kFs));
    size_t n = 1; while (n < len) n <<= 1;
    std::vector<std::complex<double>> L(n), R(n);
    for (size_t i = 0; i < len; ++i) { L[i] = x.l[from + i]; R[i] = x.r[from + i]; }
    binaural::fft(L); binaural::fft(R);
    Analysis a;
    auto freq = [&](size_t i) { return (i <= n / 2 ? i : n - i) * kFs / n; };
    for (double fc = 100; fc <= 10000; fc *= std::pow(2.0, 1.0 / 3)) {
        const double lo = fc / std::pow(2.0, 1.0 / 6), hi = fc * std::pow(2.0, 1.0 / 6);
        double e = 1e-20;
        for (size_t i = 1; i < n / 2; ++i) { const double f = freq(i); if (f >= lo && f < hi) e += std::norm(L[i]) + std::norm(R[i]); }
        a.third.push_back(e);
    }
    double s = 1e-20;
    for (size_t i = 1; i < n / 2; ++i) { const double f = freq(i); if (f >= 300 && f < 4000) s += std::norm(L[i]) + std::norm(R[i]); }
    a.speech = 10 * std::log10(s);
    if (wantIacc) {
        double sum = 0;
        for (double fc : {500.0, 1000.0, 2000.0}) {
            const double lo = fc / std::sqrt(2.0), hi = fc * std::sqrt(2.0);
            auto back = [&](const std::vector<std::complex<double>>& X) {
                std::vector<std::complex<double>> b(n);
                for (size_t i = 0; i < n; ++i) { const double f = freq(i); b[i] = (f >= lo && f <= hi) ? std::conj(X[i]) : 0.0; }
                binaural::fft(b);
                std::vector<double> y(len);
                for (size_t i = 0; i < len; ++i) y[i] = b[i].real() / n;
                std::reverse(y.begin() + 1, y.end());
                return y;
            };
            sum += iacc(back(L), back(R));
        }
        a.iaccA = sum / 3;
    }
    return a;
}

static double colourOf(const Analysis& x, const Analysis& ref) {
    double mean = 0;
    for (size_t i = 0; i < x.third.size(); ++i) mean += 10 * std::log10(x.third[i] / ref.third[i]);
    mean /= x.third.size();
    double worst = 0;
    for (size_t i = 0; i < x.third.size(); ++i) worst = std::max(worst, std::abs(10 * std::log10(x.third[i] / ref.third[i]) - mean));
    return worst;
}

struct Prog {
    std::string name; Stereo in, ref; Analysis refA; bool speech = false;
    double refLoud = 0, refMono = 0, targetSp = 0, targetHp = 0;
};

static Prog makeProg(const std::string& dir, const std::string& name, bool hp, bool speech) {
    Prog p; p.name = name; p.speech = speech;
    p.in = excerpt(dir + "/" + name + ".wav", 8);
    Scene flat; flat.hp = hp;
    p.ref = render(p.in, paramsFor(flat, {}));
    p.refLoud = loudness(p.ref.l, p.ref.r, kSkip);
    const Stereo m = mono(p.ref); p.refMono = loudness(m.l, m.r, kSkip);
    p.refA = analyse(p.ref);
    p.targetHp = analyse(speakersToEars(p.in, 30)).iaccA;
    p.targetSp = p.targetHp;
    return p;
}

enum class Goal { SpMatch, HpMatch, Speech, Movie };

struct Spec {
    std::string id; bool hp; Goal goal; bool nearField = false;
    std::vector<std::string> progs, bed;          // bed: music under speech (speech/movie goals)
    Scene scene; bool searchScene = false; bool searchSteering = false;
};

struct Row { std::string prog; double loud, colour, colourRef, peak, mono, width, target, band; };
struct Eval { bool ok = false; double score = -1e9, widthErr = 0, dbr = 0; std::vector<Row> rows; std::string why; };

class Tuner {
public:
    Tuner(const Spec& s, std::vector<Prog>& progs, std::vector<Prog>& bed) : spec_(s), progs_(progs), bed_(bed) {}

    Eval eval(const Scene& sc, const Stage& st) {
        const std::string key = keyOf(sc, st);
        if (auto it = memo_.find(key); it != memo_.end()) return it->second;
        Eval e = measure(sc, st);
        memo_[key] = e;
        return e;
    }
    size_t evaluations() const { return memo_.size(); }

private:
    static std::string keyOf(const Scene& sc, const Stage& st) {
        char b[200];
        std::snprintf(b, sizeof b, "%d%d%d%g|%g|%g|%g|%g|%g|%g|%g", sc.hp, sc.surround, sc.room, sc.amount, sc.cw, sc.depth, st.width, st.center, st.damping, st.cross, 0.0);
        return b;
    }

    // Renders of the scene with no stage settings: the base colour is compared
    // against, and the dialogue ratio is measured from.
    struct Base { Analysis a; double width = 0, loud = 0, peak = 0, mono = 0; };
    // The steering belongs to what is being tuned, so the base keeps the
    // preset's own steering (the defaults for a searched scene).
    std::vector<Base>& baseOf(const Scene& scene, std::vector<Prog>& ps, std::map<std::string, std::vector<Base>>& cache) {
        Scene sc = scene; sc.cw = spec_.scene.cw; sc.depth = spec_.scene.depth;
        const std::string k = keyOf(sc, {});
        auto it = cache.find(k);
        if (it != cache.end()) return it->second;
        std::vector<Base> v;
        for (auto& p : ps) {
            const Stereo x = render(p.in, paramsFor(sc, {}));
            Base b; b.a = analyse(x, spec_.hp); b.width = p.speech ? 0 : widthOf(x, spec_.hp);
            b.loud = loudness(x.l, x.r, kSkip) - p.refLoud;
            b.peak = peakDb(x, kSkip) + 6;
            if (!spec_.hp) { const Stereo m = mono(x); b.mono = loudness(m.l, m.r, kSkip) - p.refMono; }
            v.push_back(b);
        }
        return cache[k] = v;
    }

    double widthOf(const Stereo& x, bool hp) const {
        if (hp) return analyse(x).iaccA;
        return analyse(speakersToEars(x, 15)).iaccA;
    }

    double targetOf(const Prog& p, bool hp) const { return hp ? p.targetHp : p.targetSp; }

    Eval measure(const Scene& sc, const Stage& st) {
        Eval e; e.ok = true;
        const ChainParams cp = paramsFor(sc, st);
        auto& base = baseOf(sc, progs_, baseCache_);
        double errSum = 0, errBase = 0; int nErr = 0;
        double speechGain = 0, bedGain = 0, musicGain = 0; int nS = 0, nB = 0, nM = 0;
        for (size_t k = 0; k < progs_.size(); ++k) {
            const Prog& p = progs_[k];
            const Stereo x = render(p.in, cp);
            const Analysis xa = analyse(x, spec_.hp);
            Row r{p.name};
            r.loud = loudness(x.l, x.r, kSkip) - p.refLoud;
            r.colour = colourOf(xa, base[k].a);
            r.colourRef = colourOf(xa, p.refA);
            r.peak = peakDb(x, kSkip);
            r.mono = 0;
            if (!spec_.hp) { const Stereo m = mono(x); r.mono = loudness(m.l, m.r, kSkip) - p.refMono; }
            r.target = targetOf(p, spec_.hp);
            r.band = xa.speech - base[k].a.speech;
            if (p.speech) {
                r.width = 0;
                speechGain += r.band; ++nS;
            } else {
                musicGain += r.band; ++nM;
                r.width = spec_.hp ? xa.iaccA : widthOf(x, false);
                errSum += std::abs(r.width - r.target); ++nErr;
                errBase += std::abs(base[k].width - r.target);
            }
            // A searched scene may not colour the recording beyond what the
            // existing surround choices measured (3.5 dB; the 7.1 layout itself
            // is 5.1 dB and is left to the fixed presets).
            if (spec_.searchScene && r.colourRef > 4.0) { e.ok = false; e.why += " colourRef(" + p.name + ")"; }
            const double colourLimit = p.speech ? 2.0 : (spec_.goal == Goal::Speech ? 6.0 : 3.0);
            r.peak += 6;   // as for a master peaking at -1 dBFS
            // What the stage settings add on top of the scene.
            if (std::abs(r.loud - base[k].loud) > 1.0) { e.ok = false; e.why += " level(" + p.name + ")"; }
            if (r.peak - base[k].peak > 1.0) { e.ok = false; e.why += " peak(" + p.name + ")"; }
            if (r.colour > colourLimit) { e.ok = false; e.why += " colour(" + p.name + ")"; }
            if (!spec_.hp && r.mono - base[k].mono < -1.0) { e.ok = false; e.why += " mono(" + p.name + ")"; }
            // A scene that is itself being chosen also answers to Reference.
            if (spec_.searchScene) {
                if (std::abs(r.loud) > 1.5) { e.ok = false; e.why += " levelRef(" + p.name + ")"; }
                if (r.peak > 1.0) { e.ok = false; e.why += " peakRef(" + p.name + ")"; }
                if (!spec_.hp && r.mono < -1.0) { e.ok = false; e.why += " monoRef(" + p.name + ")"; }
            }
            e.rows.push_back(r);
        }
        if (!bed_.empty()) {
            auto& bedBase = baseOf(sc, bed_, bedCache_);
            for (size_t k = 0; k < bed_.size(); ++k) {
                const Stereo x = render(bed_[k].in, cp);
                bedGain += analyse(x, false).speech - bedBase[k].a.speech; ++nB;
                const double loud = loudness(x.l, x.r, kSkip) - bed_[k].refLoud;
                if (std::abs(loud - bedBase[k].loud) > 1.0) { e.ok = false; e.why += " level(" + bed_[k].name + ")"; }
                if (peakDb(x, kSkip) + 6 - bedBase[k].peak > 1.0) { e.ok = false; e.why += " peak(" + bed_[k].name + ")"; }
                if (!spec_.hp) {
                    const Stereo m = mono(x);
                    if (loudness(m.l, m.r, kSkip) - bed_[k].refMono - bedBase[k].mono < -1.0) { e.ok = false; e.why += " mono(" + bed_[k].name + ")"; }
                }
            }
        }
        e.widthErr = nErr ? errSum / nErr : 0;
        const double widthErrBase = nErr ? errBase / nErr : 0;
        e.dbr = (nS ? speechGain / nS : 0) - (nB ? bedGain / nB : (nM ? musicGain / nM : 0));
        switch (spec_.goal) {
        case Goal::SpMatch: case Goal::HpMatch: e.score = -e.widthErr; break;
        case Goal::Speech: e.score = e.dbr; break;
        case Goal::Movie:
            e.score = e.dbr;
            if (nErr && e.widthErr > widthErrBase + 0.02) { e.ok = false; e.why += " width"; }
            break;
        }
        return e;
    }

    const Spec& spec_;
    std::vector<Prog>& progs_;
    std::vector<Prog>& bed_;
    std::map<std::string, Eval> memo_;
    std::map<std::string, std::vector<Base>> baseCache_, bedCache_;
};

// Ties go to the smaller change: a value only moves if it measurably helps.
// Width targets are in IACC (1e-4 per step unit); the dialogue ratio is in dB
// and has to improve by 0.002 dB per unit — +40 on a control must buy 0.08 dB.
static double gEffortScale = 1;
static double effort(const Stage& g, const Scene& s) {
    return gEffortScale * (1e-4 * (std::abs(g.width) + g.center + g.damping + g.cross) + (s.room ? 1e-4 * s.amount : 0) + 1e-3 * (s.surround != 0));
}

static bool better(const Eval& a, double ea, const Eval& b, double eb) {
    if (a.ok != b.ok) return a.ok;
    if (!a.ok) return ea < eb;     // nothing holds the limits: move as little as possible
    return a.score - ea > b.score - eb;
}

int main(int argc, char** argv) {
    if (argc < 3) return 64;
    const std::string dir = argv[1];
    const std::vector<std::string> generalMusic = {"pop1", "cand1", "rock1", "hiphop1", "edm1", "jazz1", "shosta", "folk1"};
    const std::vector<std::string> speechProgs = {"speech1", "speech2"};
    const std::vector<std::string> speechBed = {"pop1", "orch1"};
    std::map<std::string, std::vector<std::string>> genre = {
        {"pop", {"pop1", "pop2"}}, {"ballad", {"cand1", "cand3"}}, {"rock", {"rock1", "rock2"}}, {"hiphop", {"hiphop1", "hiphop2"}},
        {"electronic", {"edm1", "edm2"}}, {"jazz", {"jazz1", "jazz2"}}, {"classical", {"shosta", "classical1"}}, {"acoustic", {"folk1", "folk2"}},
    };
    auto sc = [](bool hp, int surround, int room, double amount, double cw = 100, double depth = 50) {
        Scene s; s.hp = hp; s.surround = surround; s.room = room; s.amount = amount; s.cw = cw; s.depth = depth; return s;
    };
    // id -> spec. Scenes of the existing presets stay as measured before (their
    // identity); their stage values are measured here. Genre presets measure
    // the scene too.
    std::vector<Spec> specs = {
        {"sp-reference", false, Goal::SpMatch, false, {}, {}, sc(false, 0, 0, 50)},
        {"sp-voice", false, Goal::Speech, false, speechProgs, speechBed, sc(false, 0, 0, 50)},
        {"sp-podcast", false, Goal::Speech, false, speechProgs, speechBed, sc(false, 0, 1, 100)},
        {"sp-meeting", false, Goal::Speech, false, speechProgs, speechBed, sc(false, 0, 2, 50)},
        {"sp-living", false, Goal::SpMatch, false, generalMusic, {}, sc(false, 0, 2, 50)},
        {"sp-studio", false, Goal::SpMatch, false, generalMusic, {}, sc(false, 0, 1, 100)},
        {"sp-lounge", false, Goal::SpMatch, false, generalMusic, {}, sc(false, 0, 2, 80)},
        {"sp-ambience", false, Goal::SpMatch, false, generalMusic, {}, sc(false, 1, 0, 50)},
        {"sp-chamber", false, Goal::SpMatch, false, generalMusic, {}, sc(false, 0, 3, 40)},
        {"sp-concert", false, Goal::SpMatch, false, generalMusic, {}, sc(false, 0, 3, 70)},
        {"sp-gaming", false, Goal::SpMatch, true, generalMusic, {}, sc(false, 2, 1, 100)},
        {"sp-hometheater", false, Goal::Movie, false, {"orch1", "orch2", "speech1"}, {}, sc(false, 2, 2, 65)},
        {"sp-widestage", false, Goal::SpMatch, true, generalMusic, {}, sc(false, 2, 0, 50)},
        {"sp-cinema", false, Goal::Movie, false, {"orch1", "orch2", "speech1"}, {}, sc(false, 2, 3, 40)},
        {"sp-tvshow", false, Goal::Speech, false, speechProgs, speechBed, sc(false, 1, 0, 50)},
        {"hp-reference", true, Goal::HpMatch, false, {}, {}, sc(true, 0, 0, 50)},
        {"hp-voice", true, Goal::Speech, false, speechProgs, speechBed, sc(true, 0, 0, 50)},
        {"hp-podcast", true, Goal::Speech, false, speechProgs, speechBed, sc(true, 0, 1, 50)},
        {"hp-living", true, Goal::HpMatch, false, generalMusic, {}, sc(true, 0, 2, 40)},
        {"hp-ambience", true, Goal::HpMatch, false, generalMusic, {}, sc(true, 1, 0, 50)},
        {"hp-studio", true, Goal::HpMatch, false, generalMusic, {}, sc(true, 0, 1, 80)},
        {"hp-lounge", true, Goal::HpMatch, false, generalMusic, {}, sc(true, 0, 2, 60)},
        {"hp-concert", true, Goal::HpMatch, false, generalMusic, {}, sc(true, 0, 3, 35)},
        {"hp-surroundmusic", true, Goal::HpMatch, false, generalMusic, {}, sc(true, 2, 0, 50), false, true},
        {"hp-orchestra", true, Goal::HpMatch, false, {"shosta", "orch1", "orch2"}, {}, sc(true, 2, 3, 50), false, true},
        {"hp-hometheater", true, Goal::Movie, false, {"orch1", "orch2", "speech1"}, {}, sc(true, 2, 1, 50), false, true},
        {"hp-cinema71", true, Goal::Movie, false, {"orch1", "orch2", "speech1"}, {}, sc(true, 3, 0, 50), false, true},
        {"hp-living71", true, Goal::HpMatch, false, generalMusic, {}, sc(true, 3, 2, 40), false, true},
        {"hp-gaming71", true, Goal::HpMatch, false, generalMusic, {}, sc(true, 3, 1, 40, 100, 80), false, true},
        {"hp-dialogue51", true, Goal::Speech, false, speechProgs, speechBed, sc(true, 2, 0, 50), false, true},
    };
    for (const auto& [g, progs] : genre) {
        specs.push_back({"sp-" + g, false, Goal::SpMatch, false, progs, {}, sc(false, 0, 0, 50), true});
        specs.push_back({"hp-" + g, true, Goal::HpMatch, false, progs, {}, sc(true, 0, 0, 50), true, false});
    }

    for (int a = 2; a < argc; ++a) {
        const std::string want = argv[a];
        auto it = std::find_if(specs.begin(), specs.end(), [&](const Spec& s) { return s.id == want; });
        if (it == specs.end()) { std::fprintf(stderr, "unknown %s\n", want.c_str()); continue; }
        const Spec spec = *it;
        std::vector<Prog> progs, bed;
        for (auto& n : spec.progs) progs.push_back(makeProg(dir, n, spec.hp, n.rfind("speech", 0) == 0));
        for (auto& n : spec.bed) bed.push_back(makeProg(dir, n, spec.hp, false));
        Tuner t(spec, progs, bed);
        gEffortScale = (spec.goal == Goal::Speech || spec.goal == Goal::Movie) ? 20 : 1;
        Scene scene = spec.scene; Stage stage;
        if (spec.id.find("reference") != std::string::npos) {
            std::printf("RESULT %s surround=0 room=0 amount=50 cw=100 depth=50 width=0 center=0 damping=0 cross=0\n", spec.id.c_str());
            continue;
        }
        // 1. Scene (genre presets): every surround choice for the output, no room
        //    or each room at four levels.
        if (spec.searchScene) {
            Eval best; double bestEffort = 0; bool have = false;
            const int maxSurround = spec.hp ? 3 : 2;
            for (int s = 0; s <= maxSurround; ++s)
                for (int room = 0; room <= 3; ++room)
                    for (double amount : room ? std::vector<double>{25, 50, 75, 100} : std::vector<double>{50}) {
                        Scene c = scene; c.surround = s; c.room = room; c.amount = amount;
                        const Eval e = t.eval(c, stage);
                        const double ef = effort(stage, c);
                        if (!have || better(e, ef, best, bestEffort)) { best = e; bestEffort = ef; scene = c; have = true; }
                    }
        }
        // 2. Stage (and upmix steering on headphone layouts), coordinate descent.
        const std::vector<double> widths = {-200, -160, -120, -80, -60, -40, -20, 0, 20, 40, 60, 80, 120};
        const std::vector<double> levels = {0, 10, 20, 30, 40, 60, 80};
        const std::vector<double> crosses = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
        const std::vector<double> steer = {0, 25, 50, 75, 100};
        // The engine zeroes headphone crossfeed under an upmix (measured: no
        // difference), so it is not searched there.
        const bool crossRenders = !(spec.hp && scene.surround >= 2);
        const bool steering = spec.hp && scene.surround >= 2;
        for (int pass = 0; pass < 3; ++pass) {
            bool moved = false;
            auto sweep = [&](const std::vector<double>& values, std::function<void(Scene&, Stage&, double)> set, std::function<double(const Scene&, const Stage&)> get) {
                Scene bs = scene; Stage bg = stage; Eval be = t.eval(scene, stage); double bef = effort(stage, scene);
                for (double v : values) {
                    Scene cs = scene; Stage cg = stage; set(cs, cg, v);
                    const Eval e = t.eval(cs, cg); const double ef = effort(cg, cs);
                    if (getenv("TUNE_DEBUG")) std::fprintf(stderr, "  try s%d r%d a%g cw%g d%g | w%g c%g d%g x%g -> ok %d score %.4f%s\n", cs.surround, cs.room, cs.amount, cs.cw, cs.depth, cg.width, cg.center, cg.damping, cg.cross, e.ok, e.score, e.why.c_str());
                    if (better(e, ef, be, bef)) { be = e; bef = ef; bs = cs; bg = cg; }
                }
                if (get(bs, bg) != get(scene, stage)) moved = true;
                scene = bs; stage = bg;
            };
            sweep(widths, [](Scene&, Stage& g, double v) { g.width = v; }, [](const Scene&, const Stage& g) { return g.width; });
            sweep(levels, [](Scene&, Stage& g, double v) { g.center = v; }, [](const Scene&, const Stage& g) { return g.center; });
            sweep(levels, [](Scene&, Stage& g, double v) { g.damping = v; }, [](const Scene&, const Stage& g) { return g.damping; });
            if (crossRenders) sweep(crosses, [](Scene&, Stage& g, double v) { g.cross = v; }, [](const Scene&, const Stage& g) { return g.cross; });
            if (steering) {
                sweep(steer, [](Scene& s, Stage&, double v) { s.cw = v; }, [](const Scene& s, const Stage&) { return s.cw; });
                sweep(steer, [](Scene& s, Stage&, double v) { s.depth = v; }, [](const Scene& s, const Stage&) { return s.depth; });
            }
            if (!moved) break;
        }
        const Eval e = t.eval(scene, stage);
        const Eval base = t.eval(scene, Stage{});
        std::printf("RESULT %s surround=%d room=%d amount=%g cw=%g depth=%g width=%g center=%g damping=%g cross=%g ok=%d score=%.4f base=%.4f widthErr=%.4f dbr=%+.2f evals=%zu%s\n",
                    spec.id.c_str(), scene.surround, scene.room, scene.amount, scene.cw, scene.depth, stage.width, stage.center, stage.damping, stage.cross,
                    e.ok, e.score, base.score, e.widthErr, e.dbr, t.evaluations(), e.ok ? "" : (" why:" + e.why).c_str());
        for (const Row& r : e.rows)
            std::printf("  %-10s level %+5.2f colour %4.1f (vs Reference %4.1f) peak %+5.1f mono %+5.2f width %.3f target %.3f band %+5.2f\n",
                        r.prog.c_str(), r.loud, r.colour, r.colourRef, r.peak, r.mono, r.width, r.target, r.band);
        std::fflush(stdout);
    }
}
