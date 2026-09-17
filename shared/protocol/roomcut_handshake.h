/*
 * roomcut_handshake.h — the HELLO handshake's Mach-message wire format.
 *
 * Split out from roomcut_ipc.h because it depends on <mach/message.h>; keeping
 * it separate lets roomcut_ipc.h / roomcut_ring.h stay dependency-free for the
 * sandboxed driver's real-time path and for plain-C consumers.
 *
 * The handshake bootstraps the shared-memory ring out of the coreaudiod
 * sandbox: the driver sends a request carrying its requested format and a reply
 * send-right (in msgh_local_port); the engine creates the RingRegion and
 * replies with the negotiated format plus the memory-entry send-right as a Mach
 * PORT descriptor. A port descriptor is the only way a sandboxed driver can
 * receive the region — see DEVELOPMENT_PLAN.md §4.1 and the roomcut_ipc.h top
 * comment.
 *
 * A complex mach message lays out: header, then mach_msg_body_t, then the
 * descriptors, then inline data. The request is simple (its only "right" is the
 * reply port in the header); the reply is complex (one port descriptor).
 */
#ifndef ROOMCUT_HANDSHAKE_H
#define ROOMCUT_HANDSHAKE_H

#include <stdint.h>
#include <stddef.h>

#include <mach/message.h>

#include "roomcut_ipc.h"

#define ROOMCUT_CAP_SPATIAL_PARAMS 0x00000001u
#define ROOMCUT_CAP_PARAMETRIC     0x00000002u
#define ROOMCUT_CAP_ANALYZER       0x00000004u
#define ROOMCUT_CAP_VOLUME_BOOST   0x00000008u
#define ROOMCUT_CAP_DYNAMICS       0x00000010u /* highpassHz/compAmount on the wire */
#define ROOMCUT_CAP_LEVEL_MATCH    0x00000020u
#define ROOMCUT_CAP_DYNAMIC_EQ     0x00000040u /* per-band dynamic EQ on the wire */
#define ROOMCUT_CAP_VIRTUAL_ROOM   0x00000080u /* roomType/roomAmount on the wire */
#define ROOMCUT_CAP_HEAD_TRACKING  0x00000100u /* SET_HEAD_POSE + head-tracked render */
#define ROOMCUT_CAP_UPMIX          0x00000200u /* surroundType/center+surroundDepth on the wire */
#define ROOMCUT_CAP_BED_RENDERER   0x00000400u /* bedRenderer on the wire + bed renderer status */

/* Driver → engine: request the handoff. Sent to the engine's service port;
 * header.msgh_local_port carries a reply send-once right. */
typedef struct {
    mach_msg_header_t        header;
    uint32_t                 msgType;          /* ROOMCUT_MSG_HELLO */
    uint32_t                 protocolVersion;  /* ROOMCUT_IPC_VERSION */
    RoomcutFormatNegotiation requested;
} RoomcutHelloRequest;

/* Engine → driver: the negotiated format + the memory-entry send-right.
 *
 * availableRates[] carries the REAL output device's supported nominal sample
 * rates so the driver can advertise them as its own — that way coreaudiod picks
 * a rate the real device supports and the engine's render path is a passthrough
 * (no muffling from a stale/mismatched ring rate). Appended at the end so an
 * older driver that doesn't read them still parses `granted` correctly. */
typedef struct {
    mach_msg_header_t          header;
    mach_msg_body_t            body;         /* msgh_descriptor_count == 1 */
    mach_msg_port_descriptor_t memoryEntry;  /* send-right to the ring region */
    uint32_t                   msgType;      /* ROOMCUT_MSG_HELLO */
    uint32_t                   status;       /* 0 == accepted */
    RoomcutFormatNegotiation   granted;
    uint32_t                   availableRateCount;            /* 0 = none reported */
    uint32_t                   availableRates[ROOMCUT_MAX_RATES];
} RoomcutHelloReply;

/* Receive buffer large enough for either message plus its trailer. */
typedef union {
    RoomcutHelloRequest request;
    RoomcutHelloReply   reply;
    struct {
        mach_msg_header_t  header;
        char               space[512];
        mach_msg_trailer_t trailer;
    } raw;
} RoomcutHelloMsgBuffer;

static inline int roomcut_hello_request_valid(const RoomcutHelloRequest* request) {
    return request->header.msgh_size >= sizeof(*request) &&
        !(request->header.msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
        request->header.msgh_id == ROOMCUT_MSG_HELLO &&
        MACH_MSGH_BITS_REMOTE(request->header.msgh_bits) == MACH_MSG_TYPE_PORT_SEND_ONCE &&
        MACH_PORT_VALID(request->header.msgh_remote_port) &&
        request->msgType == ROOMCUT_MSG_HELLO && request->protocolVersion == ROOMCUT_IPC_VERSION;
}

static inline int roomcut_hello_format_valid(const RoomcutFormatNegotiation* granted) {
    const uint32_t capacity = granted->capacityFrames;
    return granted->channels == ROOMCUT_MVP_CHANNELS && roomcut_sr_supported(granted->sampleRate) &&
        granted->channelLayout == ROOMCUT_LAYOUT_STEREO && granted->internalFormat == ROOMCUT_INTERNAL_FORMAT_F32 &&
        capacity >= 256u && capacity <= 65536u && (capacity & (capacity - 1u)) == 0;
}

static inline int roomcut_hello_reply_valid(const RoomcutHelloReply* reply) {
    /* The appended device rates are optional for compatibility with v1 peers. */
    return reply->header.msgh_size >= offsetof(RoomcutHelloReply, availableRateCount) &&
        (reply->header.msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
        reply->header.msgh_id == ROOMCUT_MSG_HELLO && reply->header.msgh_remote_port == MACH_PORT_NULL &&
        reply->body.msgh_descriptor_count == 1 && reply->memoryEntry.type == MACH_MSG_PORT_DESCRIPTOR &&
        reply->memoryEntry.disposition == MACH_MSG_TYPE_PORT_SEND && MACH_PORT_VALID(reply->memoryEntry.name) &&
        reply->msgType == ROOMCUT_MSG_HELLO && reply->status == 0 && roomcut_hello_format_valid(&reply->granted);
}

/* ---- Heartbeat (HEALTH_CHECK) wire format ----
 *
 * Liveness probe in both directions (DEVELOPMENT_PLAN.md §4.1 "heartbeat both
 * ways", §4.2 "engine killed → heartbeat timeout"). A prober sends a request to
 * the peer's service/health port carrying a reply send-once right; the peer
 * echoes back the sequence and its coarse RoomcutEngineState. A missed reply
 * within the timeout means the peer is gone → the driver transitions to
 * EngineLost (and the engine to Recovering). Simple inline messages, no
 * descriptors. */
typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_HEALTH_CHECK */
    uint32_t          sequence;  /* echoed back by the responder */
} RoomcutHealthRequest;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_HEALTH_CHECK */
    uint32_t          sequence;  /* must match the request */
    uint32_t          state;     /* responder's RoomcutEngineState */
    /* The real output device's supported rates, so the driver can correct its
     * nominal rate after a live output-device switch (which doesn't re-HELLO).
     * Appended after the original fields (was _pad0 + nothing) so an older
     * driver that doesn't read them still parses state correctly. 0 = none. */
    uint32_t          availableRateCount;
    uint32_t          availableRates[ROOMCUT_MAX_RATES];
} RoomcutHealthReply;

typedef union {
    RoomcutHealthRequest request;
    RoomcutHealthReply   reply;
    struct {
        mach_msg_header_t  header;
        char               space[256];
        mach_msg_trailer_t trailer;
    } raw;
} RoomcutHealthMsgBuffer;

/* Validate received messages before reading fields: the Mach trailer starts
 * at msgh_size and must never stand in for a missing sequence or state. */
static inline int roomcut_health_request_valid(const RoomcutHealthRequest* request) {
    return request->header.msgh_size >= sizeof(*request) &&
        !(request->header.msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
        request->header.msgh_id == ROOMCUT_MSG_HEALTH_CHECK &&
        MACH_MSGH_BITS_REMOTE(request->header.msgh_bits) == MACH_MSG_TYPE_PORT_SEND_ONCE &&
        MACH_PORT_VALID(request->header.msgh_remote_port) &&
        request->msgType == ROOMCUT_MSG_HEALTH_CHECK;
}

static inline int roomcut_health_reply_valid(const RoomcutHealthReply* reply, uint32_t sequence) {
    /* Older engines omit the appended rates; the original state is required. */
    return reply->header.msgh_size >= offsetof(RoomcutHealthReply, availableRateCount) &&
        !(reply->header.msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
        reply->header.msgh_id == ROOMCUT_MSG_HEALTH_CHECK &&
        reply->header.msgh_remote_port == MACH_PORT_NULL &&
        reply->msgType == ROOMCUT_MSG_HEALTH_CHECK &&
        reply->sequence == sequence && reply->state <= ROOMCUT_STATE_RECOVER;
}

/* ---- Control plane (app/CLI → engine), Phase 6 ----
 *
 * Same shape as the heartbeat: an inline request sent to the engine's service
 * port carrying a reply send-once right, answered synchronously. The engine is
 * the authority — preset ids are resolved against its builtin table and every
 * parameter set passes PresetValidator::clamp() before reaching the render
 * thread. */

#define ROOMCUT_PRESET_ID_MAX 32
#define ROOMCUT_DEVICE_UID_MAX 128

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;                         /* ROOMCUT_MSG_SET_PRESET */
    char              presetId[ROOMCUT_PRESET_ID_MAX]; /* NUL-terminated */
} RoomcutSetPresetRequest;

/* Pin the real output device the engine renders to. Empty uid = return to the
 * automatic policy (real default → saved real → builtin → any). */
typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;                          /* ROOMCUT_MSG_SET_OUTPUT_DEV */
    uint32_t          _pad0;
    char              deviceUID[ROOMCUT_DEVICE_UID_MAX]; /* NUL-terminated; "" = auto */
} RoomcutSetDeviceRequest;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_SET_BYPASS */
    uint32_t          bypass;    /* 0 = off, 1 = on */
} RoomcutSetBypassRequest;

/* When on, the engine reclaims Roomcut as the system default output if macOS
 * switches it away (e.g. AirPods connect), routing through the new device. */
typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_SET_KEEP_DEFAULT */
    uint32_t          on;        /* 0 = off, 1 = on */
} RoomcutSetKeepDefaultRequest;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;
    uint32_t          _pad0;
    double            boost;
} RoomcutSetVolumeBoostRequest;

/* Custom DSP parameter set (live EQ from the app/CLI). Mirrors the MVP
 * ChainParams: preamp, 10 graphic-EQ band gains, limiter, output gain. The
 * engine runs every field through PresetValidator::clamp() before the render
 * thread sees it. 10 bands is fixed by GraphicEQ::kNumBands; spelled literally
 * here so this header stays dependency-free. */
#define ROOMCUT_EQ_BANDS 10
#define ROOMCUT_PARAM_BANDS 6
#define ROOMCUT_ANALYSIS_SPECTRUM_BINS 24

/* One parametric-EQ band. `type` indexes BiquadType (0 Bell, 1 LowShelf,
 * 2 HighShelf, 3 HighPass, 4 LowPass, 5 Notch). Two uint32 then three doubles
 * so arrays keep the doubles 8-byte aligned. */
typedef struct {
    uint32_t          enabled;   /* 0/1 */
    uint32_t          type;      /* BiquadType index */
    double            freqHz;
    double            gainDb;
    double            q;
} RoomcutParamBand;

/* Optional dynamic behaviour for one parametric band. Carried in its own array
 * appended after the static bands, so the band layout above never moves and a
 * shorter message from an older sender reads as all-zero, which is "off". */
typedef struct {
    uint32_t          enabled;   /* 0/1 */
    uint32_t          _pad0;
    double            thresholdDb;
    double            rangeDb;
    double            attackMs;
    double            releaseMs;
} RoomcutParamDynamics;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_SET_PARAMS */
    uint32_t          _pad0;      /* keep the doubles 8-byte aligned */
    double            preampDb;
    double            eqGainsDb[ROOMCUT_EQ_BANDS];
    double            limiterReleaseMs;
    double            outputGainDb;
    double            spatialWidth;
    double            centerFocus;
    double            crossfeed;
    double            roomReduce;
    double            spatialMode;  /* 0 = speaker (XTC), 1 = headphone (crossfeed) */
    RoomcutParamBand  parametric[ROOMCUT_PARAM_BANDS];
    /* Dynamics (ROOMCUT_CAP_DYNAMICS), appended at the end so a version-skewed
     * peer still parses the fields above: a short message from an older sender
     * reads as 0 (off) out of the zeroed receive buffer. */
    double            highpassHz;   /* 0 = off */
    double            compAmount;   /* 0..100 leveling amount, 0 = off */
    /* Dynamic EQ (ROOMCUT_CAP_DYNAMIC_EQ), appended for the same reason. */
    RoomcutParamDynamics dynamics[ROOMCUT_PARAM_BANDS];
    /* Virtual room (headphone only), appended for the same reason: an older
     * sender leaves both at 0, which reads as "no room". */
    double            roomType;    /* 0 = off, 1 = Studio, 2 = Living Room, 3 = Hall */
    double            roomAmount;  /* 0..100, 50 = the room's reference level */
    /* Virtual 5.1/7.1 upmix (ROOMCUT_CAP_UPMIX), appended for the same reason:
     * an older sender leaves all three at 0, which reads as "no upmix". */
    double            surroundType;     /* 0 = off, 2 = virtual 5.1, 3 = virtual 7.1 */
    double            centerWidth;    /* -12..+6 */
    double            surroundDepth;  /* -12..+6 */
    /* Headphone bed renderer (ROOMCUT_CAP_BED_RENDERER), appended: an older
     * sender leaves it 0, which is the default — the system renderer. */
    double            bedRenderer;    /* 0 = system (AUSpatialMixer) where attached, 1 = built-in */
} RoomcutSetParamsRequest;

/* Live head orientation from the listener's headphones. Unlike every other
 * SET_*, this is not a preset value: it arrives tens of times a second and is
 * never persisted, so it has its own small message instead of riding along with
 * the parameter set (which would crossfade the whole chain on every update). */
typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_SET_HEAD_POSE */
    uint32_t          active;    /* 0 = tracker not delivering; render goes dry */
    double            yawDeg;    /* + = listener turned right */
} RoomcutSetHeadPoseRequest;

/* Listening tests (PRD P0-4): a pink-noise burst on one channel of the
 * headphone upmix in place of the programme, at a fixed level, through the
 * live renderer, room and head tracking. Heard only while the chain renders a
 * 5.1/7.1 headphone bed. channel 0-6 = C, L, R, Ls, Rs, Lb, Rb (Lb/Rb are
 * silent in 5.1); any other channel stops a burst that is running. */
typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_PROBE_CHANNEL */
    int32_t           channel;
    double            seconds;   /* (0, 10] */
    double            levelDb;   /* RMS dBFS on the channel, [-60, 0] */
} RoomcutProbeChannelRequest;

/* Acknowledgement for SET_* requests. */
typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* echoes the request's msgType */
    uint32_t          status;    /* 0 = applied; 1 = unknown preset */
} RoomcutControlReply;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_STATE */
    uint32_t          _pad0;
} RoomcutStateRequest;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;
    uint32_t          _pad0;
} RoomcutGetParamsRequest;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;   /* ROOMCUT_MSG_GET_ANALYSIS */
    uint32_t          _pad0;
} RoomcutAnalysisRequest;

/* Engine status snapshot (the menu-bar / CLI surface). */
typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;                  /* ROOMCUT_MSG_STATE */
    uint32_t          state;                    /* coarse RoomcutEngineState */
    uint32_t          manualBypass;             /* 0/1 */
    uint32_t          safeBypass;               /* 0/1 (latched NaN guard) */
    char              presetId[ROOMCUT_PRESET_ID_MAX];
    float             limiterGainReductionDb;   /* > 0 → clipping indicator */
    float             renderPeak;               /* last block's peak */
    uint32_t          paramsRevision;
    uint32_t          _pad1;
    uint64_t          framesRendered;
    uint64_t          ringUnderruns;
    char              outputDeviceUID[ROOMCUT_DEVICE_UID_MAX]; /* current real output; "" if none */
    uint32_t          keepDefault;  /* 0/1 — reclaim-Roomcut-as-default toggle */
    uint32_t          capabilities;
    double            volumeBoost;
    /* Latency the engine adds on purpose: the limiter's look-ahead plus the
     * resampler's group delay. Appended (see RoomcutSetParamsRequest); 0 from an
     * engine that predates the field, which reads as "not reported". */
    double            engineLatencyMs;
    /* Headphone bed renderer status (ROOMCUT_CAP_BED_RENDERER), appended; an
     * older engine leaves all of it 0, which reads as "built-in only". */
    uint32_t          bedRenderer;          /* 0 = built-in only, 1 = AUSpatialMixer attached */
    uint32_t          bedPersonalizedHrtf;  /* 1 = the unit reports a personalized HRTF in use */
    float             bedExternalGain;      /* 0..1 of the bed the attached renderer renders now */
    float             bedUnitRate;          /* Hz the units run at; 0 without one */
} RoomcutStateReply;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;
    uint32_t          _pad0;
    char              presetId[ROOMCUT_PRESET_ID_MAX];
    uint32_t          paramsRevision;
    uint32_t          _pad1;
    double            preampDb;
    double            eqGainsDb[ROOMCUT_EQ_BANDS];
    double            limiterReleaseMs;
    double            outputGainDb;
    double            spatialWidth;
    double            centerFocus;
    double            crossfeed;
    double            roomReduce;
    double            spatialMode;
    RoomcutParamBand  parametric[ROOMCUT_PARAM_BANDS];
    /* Dynamics — appended (see RoomcutSetParamsRequest). */
    double            highpassHz;
    double            compAmount;
    RoomcutParamDynamics dynamics[ROOMCUT_PARAM_BANDS];
    /* Virtual room — appended (see RoomcutSetParamsRequest). */
    double            roomType;
    double            roomAmount;
    /* Virtual 5.1/7.1 upmix — appended (see RoomcutSetParamsRequest). */
    double            surroundType;
    double            centerWidth;
    double            surroundDepth;
    /* Headphone bed renderer — appended (see RoomcutSetParamsRequest). */
    double            bedRenderer;
} RoomcutGetParamsReply;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgType;       /* ROOMCUT_MSG_GET_ANALYSIS */
    uint32_t          valid;         /* 0 = no usable signal/window yet */
    uint32_t          sampleRate;
    uint32_t          channels;
    uint64_t          framesAnalyzed;
    float             peakDb;
    float             rmsDb;
    float             crestFactor;
    float             lowEnergy;
    float             lowMidEnergy;
    float             midEnergy;
    float             highEnergy;
    float             spectralCentroid;
    float             stereoWidth;
    float             midSideRatio;
    float             correlation;
    float             muddiness;
    float             harshness;
    float             sibilance;
    float             voicePresence;
    float             reverbEstimate;
    float             dynamicRange;
    float             spectrum[ROOMCUT_ANALYSIS_SPECTRUM_BINS];
} RoomcutAnalysisReply;

/* A complete A/B update is one transaction. Existing SET_PARAMS layouts stay
 * unchanged; the new payload is versioned independently of the driver ring. */
/* 2 added RoomcutParamDynamics. 3 adds the virtual room, 4 the upmix and 5 the
 * bed renderer — all appended AFTER the two parameter blocks, never inside them:
 * RoomcutParameterValues sits twice in a row, so growing it would shift
 * `reference` and break every size-compatible peer. An older peer simply
 * sends/receives the shorter message. */
#define ROOMCUT_COMPARISON_VERSION 5u
#define ROOMCUT_COMPARISON_MIN_VERSION 2u /* still accept a peer without the room */
#define ROOMCUT_COMPARISON_PARAMETERS 0u
#define ROOMCUT_COMPARISON_PRESET 1u
typedef struct {
    double preampDb;
    double eqGainsDb[ROOMCUT_EQ_BANDS];
    double limiterReleaseMs;
    double outputGainDb;
    double spatialWidth;
    double centerFocus;
    double crossfeed;
    double roomReduce;
    double spatialMode;
    double highpassHz;
    double compAmount;
    RoomcutParamBand parametric[ROOMCUT_PARAM_BANDS];
    RoomcutParamDynamics dynamics[ROOMCUT_PARAM_BANDS];
    /* No virtual room or upmix here — see ROOMCUT_COMPARISON_VERSION. Both
     * travel in the trailing fields of the request/reply instead. */
} RoomcutParameterValues;

typedef struct {
    mach_msg_header_t header;
    uint32_t msgType;
    uint32_t version;
    uint32_t enabled;
    uint32_t kind;
    char presetId[ROOMCUT_PRESET_ID_MAX];
    RoomcutParameterValues current;
    RoomcutParameterValues reference;
    /* Virtual room for each side (version 3+). A version-2 sender omits these
     * and the engine keeps whatever room is already playing. */
    double currentRoomType;
    double currentRoomAmount;
    double referenceRoomType;
    double referenceRoomAmount;
    /* Upmix for each side (version 4+). A version-3 sender omits these and the
     * engine keeps whatever upmix is already playing. */
    double currentSurroundType;
    double currentCenterWidth;
    double currentSurroundDepth;
    double referenceSurroundType;
    double referenceCenterWidth;
    double referenceSurroundDepth;
    /* Bed renderer for each side (version 5+). A version-4 sender omits these
     * and the engine keeps whichever renderer is already playing. */
    double currentBedRenderer;
    double referenceBedRenderer;
} RoomcutComparisonRequest;

typedef struct {
    mach_msg_header_t header;
    uint32_t msgType;
    uint32_t version;
} RoomcutGetComparisonRequest;

typedef struct {
    mach_msg_header_t header;
    uint32_t msgType;
    uint32_t version;
    uint32_t enabled;
    uint32_t state; /* 0 off, 1 measuring, 2 matched, 3 no signal, 4 bypass, 5 unavailable */
    uint64_t revision;
    uint64_t renderedRevision;
    float currentReductionDb;
    float referenceReductionDb;
    char presetId[ROOMCUT_PRESET_ID_MAX];
    RoomcutParameterValues current;
    RoomcutParameterValues reference;
    /* Virtual room per side (version 3+), appended — see the request. */
    double currentRoomType;
    double currentRoomAmount;
    double referenceRoomType;
    double referenceRoomAmount;
    /* Upmix per side (version 4+), appended — see the request. */
    double currentSurroundType;
    double currentCenterWidth;
    double currentSurroundDepth;
    double referenceSurroundType;
    double referenceCenterWidth;
    double referenceSurroundDepth;
    /* Bed renderer per side (version 5+), appended — see the request. */
    double currentBedRenderer;
    double referenceBedRenderer;
} RoomcutComparisonReply;

typedef union {
    RoomcutSetPresetRequest setPreset;
    RoomcutSetDeviceRequest setDevice;
    RoomcutSetBypassRequest setBypass;
    RoomcutSetKeepDefaultRequest setKeepDefault;
    RoomcutSetVolumeBoostRequest setVolumeBoost;
    RoomcutSetParamsRequest setParams;
    RoomcutSetHeadPoseRequest setHeadPose;
    RoomcutProbeChannelRequest probeChannel;
    RoomcutStateRequest     stateRequest;
    RoomcutGetParamsRequest getParams;
    RoomcutAnalysisRequest  analysisRequest;
    RoomcutControlReply     reply;
    RoomcutStateReply       stateReply;
    RoomcutGetParamsReply   paramsReply;
    RoomcutAnalysisReply    analysisReply;
    RoomcutComparisonRequest comparisonRequest;
    RoomcutGetComparisonRequest getComparison;
    RoomcutComparisonReply comparisonReply;
    struct {
        mach_msg_header_t  header;
        char               space[sizeof(RoomcutComparisonReply) + sizeof(mach_msg_max_trailer_t)];
        mach_msg_trailer_t trailer;
    } raw;
} RoomcutControlMsgBuffer;

#endif /* ROOMCUT_HANDSHAKE_H */
