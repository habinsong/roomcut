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

#include <mach/message.h>

#include "roomcut_ipc.h"

#define ROOMCUT_CAP_SPATIAL_PARAMS 0x00000001u
#define ROOMCUT_CAP_PARAMETRIC     0x00000002u
#define ROOMCUT_CAP_ANALYZER       0x00000004u
#define ROOMCUT_CAP_VOLUME_BOOST   0x00000008u
#define ROOMCUT_CAP_DYNAMICS       0x00000010u /* highpassHz/compAmount on the wire */

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
} RoomcutSetParamsRequest;

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

typedef union {
    RoomcutSetPresetRequest setPreset;
    RoomcutSetDeviceRequest setDevice;
    RoomcutSetBypassRequest setBypass;
    RoomcutSetKeepDefaultRequest setKeepDefault;
    RoomcutSetVolumeBoostRequest setVolumeBoost;
    RoomcutSetParamsRequest setParams;
    RoomcutStateRequest     stateRequest;
    RoomcutGetParamsRequest getParams;
    RoomcutAnalysisRequest  analysisRequest;
    RoomcutControlReply     reply;
    RoomcutStateReply       stateReply;
    RoomcutGetParamsReply   paramsReply;
    RoomcutAnalysisReply    analysisReply;
    struct {
        mach_msg_header_t  header;
        char               space[512];   /* headroom so this stays the largest union
                                            member → the receive buffer always has
                                            room for the message + Mach trailer */
        mach_msg_trailer_t trailer;
    } raw;
} RoomcutControlMsgBuffer;

#endif /* ROOMCUT_HANDSHAKE_H */
