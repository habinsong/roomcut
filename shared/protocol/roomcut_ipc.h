/*
 * roomcut_ipc.h
 *
 * IPC contract between Roomcut.driver (Audio Server Plug-in, sandboxed inside
 * coreaudiod) and RoomcutAudioEngine (helper process).
 *
 * IMPORTANT — sandbox constraint:
 *   An AudioServerPlugIn cannot open arbitrary shared memory, sockets, or files.
 *   It may only reach a Mach service whose name is declared in the driver
 *   Info.plist under the key "AudioServerPlugIn_MachServices". Therefore the
 *   shared-memory ring buffer is NOT opened by name from the driver. Instead:
 *
 *     1. RoomcutAudioEngine creates the shared memory region (POSIX shm or a
 *        Mach memory entry) and hosts a Mach/XPC service.
 *     2. The driver connects to that declared Mach service and receives a
 *        send-right to the memory region (a mach port) over the connection.
 *     3. Both sides then map the SAME physical pages and use the lock-free ring
 *        buffer below for the real-time audio path. Control messages also flow
 *        over the Mach service, never over the audio ring.
 *
 *   This mirrors how Background Music's BGMDriver talks to BGMApp.
 */
#ifndef ROOMCUT_IPC_H
#define ROOMCUT_IPC_H

#include <stdint.h>
#include "roomcut_audio_format.h"

/* Declared in driver Info.plist AudioServerPlugIn_MachServices array. */
#define ROOMCUT_MACH_SERVICE_NAME   "com.roomcut.engine"

#define ROOMCUT_IPC_VERSION         1
#define ROOMCUT_RING_MAGIC          0x524D4331u /* 'RMC1' */

/* Default ring capacity (frames, power of two) the engine falls back to when the
 * driver's requested capacity is absent or out of range. ~85 ms at 48 kHz —
 * deep enough to absorb scheduling jitter between the two processes' cadences. */
#define ROOMCUT_DEFAULT_CAPACITY_FRAMES   4096u

/*
 * Shared ring-buffer header. Lives at the start of the shared region, followed
 * by `capacityFrames * channels` interleaved float32 samples.
 *
 * Single producer (driver write thread) / single consumer (engine read thread).
 * writeIndex/readIndex are free-running frame counters; mask with capacity.
 * Use atomic loads/stores with acquire/release ordering — never a lock.
 */
typedef struct {
    uint32_t magic;            /* ROOMCUT_RING_MAGIC */
    uint32_t version;          /* ROOMCUT_IPC_VERSION */
    uint32_t capacityFrames;   /* power of two */
    uint32_t channels;         /* ROOMCUT_MVP_CHANNELS for MVP */
    uint32_t sampleRate;       /* current SR, e.g. 48000 */
    uint32_t _pad0;

    /* Producer-owned. */
    volatile uint64_t writeIndex;     /* total frames ever written */
    uint64_t hostTimeAtWrite;         /* mach_absolute_time of last write */

    /* Consumer-owned. */
    volatile uint64_t readIndex;      /* total frames ever read */

    /* Diagnostics (monotonic counters). */
    volatile uint64_t overruns;       /* producer overwrote unread data */
    volatile uint64_t underruns;      /* consumer found no data */
    volatile uint64_t dropouts;       /* engine missed a render deadline */

    /* samples[] follows immediately after this header in the mapping. */
} RoomcutRingHeader;

/* ---- Control channel (over the Mach service, request/response) ---- */

typedef enum {
    ROOMCUT_MSG_HELLO          = 1,  /* engine <-> driver handshake + shm handoff */
    ROOMCUT_MSG_SET_PRESET     = 2,
    ROOMCUT_MSG_SET_OUTPUT_DEV = 3,
    ROOMCUT_MSG_SET_BYPASS     = 4,
    ROOMCUT_MSG_SET_FORMAT     = 5,  /* sample rate / channel change */
    ROOMCUT_MSG_HEALTH_CHECK   = 6,
    ROOMCUT_MSG_STATE          = 7,  /* engine -> app status push */
    ROOMCUT_MSG_SET_PARAMS     = 8,  /* app/CLI -> engine: custom ChainParams (live EQ) */
    ROOMCUT_MSG_GET_PARAMS     = 9,  /* app/CLI -> engine: read current ChainParams */
    ROOMCUT_MSG_SET_KEEP_DEFAULT = 10, /* app/CLI -> engine: reclaim Roomcut as system default */
    ROOMCUT_MSG_GET_ANALYSIS   = 11, /* app/CLI -> engine: read latest analyzer snapshot */
    ROOMCUT_MSG_SET_VOLUME_BOOST = 12
} RoomcutMsgType;

typedef enum {
    ROOMCUT_STATE_STOPPED  = 0,
    ROOMCUT_STATE_RUNNING  = 1,
    ROOMCUT_STATE_BYPASS   = 2,
    ROOMCUT_STATE_RECOVER  = 3
} RoomcutEngineState;

/* ---- Format negotiation (DEVELOPMENT_PLAN.md §4.3) ----
 *
 * Carried in the HELLO exchange. The driver REQUESTS a format; the engine is
 * the authority and REPLIES with the format it will actually consume. A
 * mismatch triggers ring reinitialization rather than silent corruption. MVP
 * ships 2ch 44.1/48 kHz float32, but the struct is sized for later 96 kHz / 5.1
 * so the wire format does not change when those land. */
typedef enum {
    ROOMCUT_INTERNAL_FORMAT_F32 = 0   /* 32-bit float PCM, interleaved */
} RoomcutInternalFormat;

typedef enum {
    ROOMCUT_LAYOUT_STEREO = 0,
    ROOMCUT_LAYOUT_5_1    = 1,         /* reserved (post-MVP) */
    ROOMCUT_LAYOUT_7_1    = 2          /* reserved (post-MVP) */
} RoomcutChannelLayout;

typedef enum {
    ROOMCUT_LATENCY_NORMAL = 0,
    ROOMCUT_LATENCY_LOW    = 1
} RoomcutLatencyClass;

typedef struct {
    uint32_t sampleRate;       /* 44100 | 48000 (MVP); 88200/96000/... later */
    uint32_t channels;         /* 2 (MVP) */
    uint32_t channelLayout;    /* RoomcutChannelLayout */
    uint32_t internalFormat;   /* RoomcutInternalFormat */
    uint32_t bufferFrameSize;  /* ring slot sizing hint */
    uint32_t latencyClass;     /* RoomcutLatencyClass */
    uint32_t capacityFrames;   /* negotiated ring capacity (power of two) */
    uint32_t _pad0;
} RoomcutFormatNegotiation;

/* The HELLO handshake's Mach-message wire format lives in roomcut_handshake.h
 * (it depends on <mach/message.h>); roomcut_ipc.h stays dependency-free so the
 * sandboxed driver and plain-C consumers can include it without pulling Mach. */

/* ---- Internal lifecycle states ----
 *
 * These are FINER-GRAINED than RoomcutEngineState (which is the coarse status
 * pushed to the app over ROOMCUT_MSG_STATE). They are not sent on the wire;
 * each side owns its own and uses them to make the handshake + recovery
 * transitions explicit. See DEVELOPMENT_PLAN.md §4.1-4.2.
 *
 * The wire-level RoomcutEngineState is a projection of RoomcutEngineLifecycle:
 *   Starting/WaitingForDriver/Connected/BufferMapped/OutputReady -> STOPPED
 *   Streaming                                                    -> RUNNING
 *   (bypass active)                                              -> BYPASS
 *   Recovering                                                   -> RECOVER
 *
 * NOTE: the DRIVER-side projection is intentionally NOT fixed here. The driver
 * never pushes STATE to the app directly, so how EngineLost / SafeBypass surface
 * to the UI (via the engine inferring driver health from the heartbeat) is an
 * open question to settle during Phase 3 -- see DEVELOPMENT_PLAN.md §4.5.
 */

/* Driver side (runs inside coreaudiod; must stay safe when the peer is absent). */
typedef enum {
    ROOMCUT_DRIVER_UNINITIALIZED    = 0,
    ROOMCUT_DRIVER_LOADED           = 1,
    ROOMCUT_DRIVER_WAITING_ENGINE   = 2,  /* Mach service not yet reachable */
    ROOMCUT_DRIVER_HANDSHAKING      = 3,  /* HELLO sent, awaiting shm handoff */
    ROOMCUT_DRIVER_SHARED_READY     = 4,  /* region mapped + header validated */
    ROOMCUT_DRIVER_STREAMING        = 5,  /* writing frames to the ring */
    ROOMCUT_DRIVER_ENGINE_LOST      = 6,  /* heartbeat timeout; stop touching map */
    ROOMCUT_DRIVER_SAFE_BYPASS      = 7,  /* drop frames safely, retry handoff */
    ROOMCUT_DRIVER_UNLOADING        = 8
} RoomcutDriverLifecycle;

/* Engine side (owns the shared region + Mach service). */
typedef enum {
    ROOMCUT_ENGINE_STARTING         = 0,
    ROOMCUT_ENGINE_WAITING_DRIVER   = 1,  /* service published, no HELLO yet */
    ROOMCUT_ENGINE_CONNECTED        = 2,  /* HELLO received, format negotiated */
    ROOMCUT_ENGINE_BUFFER_MAPPED    = 3,  /* shared region created + handed off */
    ROOMCUT_ENGINE_OUTPUT_READY     = 4,  /* real output device opened */
    ROOMCUT_ENGINE_STREAMING        = 5,  /* read ring -> DSP -> render */
    ROOMCUT_ENGINE_RECOVERING       = 6,  /* output device or driver lost */
    ROOMCUT_ENGINE_STOPPING         = 7
} RoomcutEngineLifecycle;

#endif /* ROOMCUT_IPC_H */
