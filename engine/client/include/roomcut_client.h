/*
 * roomcut_client.h — plain-C client API over the engine's Mach control plane.
 *
 * This is the surface the native SwiftUI app (apps/macos) imports; it wraps
 * the C++ transport (engine/src/Control.cpp) so non-C++ callers never touch
 * Mach or C++ directly. Every call connects per invocation (bootstrap lookup,
 * short timeout) and is safe to call from any non-realtime thread.
 *
 * Return convention: 0 = success; < 0 = transport failure (engine not
 * reachable / timeout); > 0 = engine-reported status (e.g. 1 = unknown
 * preset).
 */
#ifndef ROOMCUT_CLIENT_H
#define ROOMCUT_CLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors RoomcutEngineState (shared/protocol/roomcut_ipc.h). */
enum {
    ROOMCUT_CLIENT_STATE_STOPPED = 0,
    ROOMCUT_CLIENT_STATE_RUNNING = 1,
    ROOMCUT_CLIENT_STATE_BYPASS  = 2,
    ROOMCUT_CLIENT_STATE_RECOVER = 3
};

/* 10 == ROOMCUT_EQ_BANDS (GraphicEQ band count) — kept literal so this header
 * stays dependency-free for Swift import. */
#define ROOMCUT_CLIENT_EQ_BANDS 10
#define ROOMCUT_CLIENT_PARAM_BANDS 6
#define ROOMCUT_CLIENT_DEVICE_UID_MAX 128
#define ROOMCUT_CLIENT_CAP_SPATIAL_PARAMS 0x00000001u
#define ROOMCUT_CLIENT_CAP_PARAMETRIC     0x00000002u
#define ROOMCUT_CLIENT_CAP_ANALYZER       0x00000004u
#define ROOMCUT_CLIENT_CAP_VOLUME_BOOST   0x00000008u
#define ROOMCUT_CLIENT_CAP_DYNAMICS       0x00000010u
#define ROOMCUT_CLIENT_ANALYSIS_SPECTRUM_BINS 24

/* One parametric-EQ band (mirrors RoomcutParamBand on the wire). `type` indexes
 * BiquadType: 0 Bell, 1 LowShelf, 2 HighShelf, 3 HighPass, 4 LowPass, 5 Notch. */
typedef struct {
    uint32_t enabled;   /* 0/1 */
    uint32_t type;
    double   freqHz;
    double   gainDb;
    double   q;
} RoomcutClientParamBand;

typedef struct {
    uint32_t state;                  /* ROOMCUT_CLIENT_STATE_* */
    uint32_t manualBypass;           /* 0/1 */
    uint32_t safeBypass;             /* 0/1 (latched NaN guard) */
    char     presetId[32];           /* NUL-terminated */
    float    limiterGainReductionDb; /* > 0 → clipping indicator */
    float    renderPeak;
    uint32_t paramsRevision;
    uint64_t framesRendered;
    uint64_t ringUnderruns;
    char     outputDeviceUID[ROOMCUT_CLIENT_DEVICE_UID_MAX]; /* current real output; "" if none */
    uint32_t keepDefault;            /* 0/1 — reclaim-Roomcut-as-default toggle */
    uint32_t capabilities;
    double   volumeBoost;
} RoomcutClientState;

typedef struct {
    double preampDb;
    double eqGainsDb[ROOMCUT_CLIENT_EQ_BANDS];
    double limiterReleaseMs;
    double outputGainDb;
    double spatialWidth;
    double centerFocus;
    double crossfeed;
    double roomReduce;
    double spatialMode;   /* 0 = speaker (XTC), 1 = headphone (crossfeed) */
    double highpassHz;    /* dynamics: 0 = off */
    double compAmount;    /* dynamics: 0..100 leveling amount, 0 = off */
    RoomcutClientParamBand parametric[ROOMCUT_CLIENT_PARAM_BANDS];
} RoomcutClientParams;

typedef struct {
    uint32_t valid;
    uint32_t sampleRate;
    uint32_t channels;
    uint64_t framesAnalyzed;
    float peakDb;
    float rmsDb;
    float crestFactor;
    float lowEnergy;
    float lowMidEnergy;
    float midEnergy;
    float highEnergy;
    float spectralCentroid;
    float stereoWidth;
    float midSideRatio;
    float correlation;
    float muddiness;
    float harshness;
    float sibilance;
    float voicePresence;
    float reverbEstimate;
    float dynamicRange;
    float spectrum[ROOMCUT_CLIENT_ANALYSIS_SPECTRUM_BINS];
} RoomcutClientAnalysis;

typedef struct {
    uint32_t bitDepth;
    double sampleRate;
    double latencyMs;
} RoomcutClientAudioFormat;

/* Fetch the engine status snapshot. */
int roomcutClientGetState(RoomcutClientState* out);

int roomcutClientGetParams(RoomcutClientParams* out);

int roomcutClientGetAnalysis(RoomcutClientAnalysis* out);

/* Apply a builtin preset by id (live, crossfaded). */
int roomcutClientSetPreset(const char* presetId);

/* Manual bypass on/off (live, crossfaded). */
int roomcutClientSetBypass(int on);

/* Keep Roomcut as the system default output: when on, the engine reclaims the
 * default if macOS switches it away (e.g. AirPods connect). */
int roomcutClientSetKeepDefault(int on);

/* Pin the real output device the engine renders to (empty/NULL uid = automatic
 * policy). Routed to the engine over the Mach control plane. */
int roomcutClientSetOutputDevice(const char* uid);

/* Enumerate the real output devices the user can pick (Roomcut's own virtual
 * device excluded; output-capable only). Resolved in-process via CoreAudio. */
int roomcutClientOutputDeviceCount(void);
int roomcutClientOutputDeviceInfo(int index, char* uidOut, int uidCap,
                                  char* nameOut, int nameCap);
int roomcutClientAudioFormat(const char* uid, RoomcutClientAudioFormat* out);

/* One physical format the real output device can run at. */
typedef struct {
    double   sampleRate;   /* Hz */
    uint32_t bitDepth;     /* mBitsPerChannel (16/24/32) */
} RoomcutClientDeviceFormat;

/* Enumerate the distinct (sampleRate, bitDepth) physical formats the real device
 * supports. Writes up to cap entries into out, returns the total count (>=0), or
 * -1 if the device/stream can't be resolved. Resolved in-process via CoreAudio. */
int roomcutClientDeviceFormatOptions(const char* uid,
                                     RoomcutClientDeviceFormat* out, int cap);

/* Set the real device's physical format (bit depth) and nominal sample rate to
 * the requested pair, which must be one of the available formats. The engine
 * polls the nominal rate and re-opens its output to match; it never touches the
 * physical format, so this won't race the render path. Returns 0 on success,
 * negative on error. */
int roomcutClientSetDeviceFormat(const char* uid, double sampleRate, unsigned bitDepth);

/* Roomcut Output device master volume (0..1), the single volume the app and the
 * macOS slider share; the engine mirrors it to the real device. get returns 0
 * on success and writes *outScalar; 1 if the device has no volume control;
 * < 0 on error. set returns 0 on success. Resolved in-process via CoreAudio. */
int roomcutClientVolumeGet(double* outScalar);
int roomcutClientVolumeSet(double scalar);

/* Output left/right balance (pan), shared with Audio MIDI Setup / System
 * Settings: implemented as the device's per-channel output volume scalars
 * (element 1 = front-left, element 2 = front-right), so reads/writes stay in
 * sync with the macOS sliders. pan is -1 (full left) .. 0 (centre) .. +1 (full
 * right): the "near" channel stays at unity and the opposite channel is
 * attenuated by |pan|. get returns 0 and writes *outPan; 1 if the device has no
 * per-channel volume control; < 0 on error. set returns 0 on success, 1 if not
 * settable, < 0 on error. Resolved in-process via CoreAudio. */
int roomcutClientBalanceGet(double* outPan);
int roomcutClientBalanceSet(double pan);

/* Make the Roomcut virtual device the system default output so app audio is
 * routed through the engine (where EQ / limiter are applied). The app calls
 * this on launch so processing is live without the user changing the macOS
 * output manually. Returns 0 if it set the default, 1 if Roomcut was already
 * default, < 0 on error. In-process CoreAudio (no engine round-trip). */
int roomcutClientMakeDefaultOutput(void);

/* Master switch OFF: route the system default output away from Roomcut, back to
 * the real device the engine renders to (or any real output device), so audio
 * bypasses Roomcut entirely. Pair with roomcutClientSetKeepDefault(0) so the
 * engine doesn't immediately reclaim the default. Returns 0 if it set the
 * default, 1 if a real device was already default, < 0 on error. In-process
 * CoreAudio (no engine round-trip). */
int roomcutClientRestoreRealDefault(void);

/* Is the Roomcut virtual device the current system default output? 1 = yes
 * (app audio routes through the engine), 0 = a real device is default, < 0 on
 * error. In-process CoreAudio. */
int roomcutClientRoomcutIsDefault(void);

/* Push a full custom parameter set (engine clamps via PresetValidator). */
int roomcutClientSetParams(double preampDb,
                           const double eqGainsDb[ROOMCUT_CLIENT_EQ_BANDS],
                           double limiterReleaseMs,
                           double outputGainDb, double spatialWidth,
                           double centerFocus, double crossfeed,
                           double roomReduce, double spatialMode,
                           double highpassHz, double compAmount,
                           const RoomcutClientParamBand parametric[ROOMCUT_CLIENT_PARAM_BANDS]);

/* Builtin preset enumeration (no engine connection needed). */
int roomcutClientPresetCount(void);
/* Fills id/name for preset `index` (0-based). Returns 0 on success. */
int roomcutClientPresetInfo(int index, char* idOut, int idCap,
                            char* nameOut, int nameCap);

#ifdef __cplusplus
}
#endif

#endif /* ROOMCUT_CLIENT_H */
