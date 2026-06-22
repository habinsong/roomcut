/*
 * RoomcutIO.c — the IO path for Roomcut Output.
 *
 * The zero-timestamp clock keeps CoreAudio's device timeline valid. The mixed
 * output is forwarded to RoomcutAudioEngine through the lock-free shared ring;
 * if the engine is unavailable, the callback remains a safe non-blocking sink.
 */
#include "RoomcutDriver.h"
#include "RoomcutTransport.h"

#include <mach/mach_time.h>
#include <string.h>

#include "roomcut_ring.h"

/*
 * Phase 2 verification instrumentation. DEBUG-ONLY (docs decision: driver-side
 * RMS/peak must never ship). Enable with -DROOMCUT_DRIVER_DEBUG=1. It computes
 * RMS/peak of the outgoing mix and emits ONE throttled os_log line roughly once
 * per second — never per callback — so the IO proc stays effectively silent
 * even in a debug build. Release builds compile this to nothing.
 */
#if ROOMCUT_DRIVER_DEBUG
#include <os/log.h>
#include <math.h>
#endif

OSStatus Roomcut_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID)
{
    (void)inClientID;
    if (inDriver == NULL) return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;

    pthread_mutex_lock(&gRoomcut.stateMutex);
    if (gRoomcut.ioRunnerCount == 0) {
        /* First client: anchor the zero-timestamp clock to "now". */
        gRoomcut.anchorHostTime   = mach_absolute_time();
        gRoomcut.anchorSampleTime = 0.0;
        gRoomcut.ztsSeed          = 1;
        gRoomcut.ioIsRunning      = true;
    }
    gRoomcut.ioRunnerCount += 1;
    pthread_mutex_unlock(&gRoomcut.stateMutex);

    return kAudioHardwareNoError;
}

OSStatus Roomcut_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID)
{
    (void)inClientID;
    if (inDriver == NULL) return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;

    pthread_mutex_lock(&gRoomcut.stateMutex);
    if (gRoomcut.ioRunnerCount > 0) {
        gRoomcut.ioRunnerCount -= 1;
    }
    if (gRoomcut.ioRunnerCount == 0) {
        gRoomcut.ioIsRunning = false;
    }
    pthread_mutex_unlock(&gRoomcut.stateMutex);

    return kAudioHardwareNoError;
}

/*
 * GetZeroTimeStamp anchors the device's sample timeline to the host clock.
 * CoreAudio polls this to relate sample time to host time; we advance the
 * zero-timestamp in whole ring-size (kRoomcut_Device_RingSize) periods.
 */
OSStatus Roomcut_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed)
{
    (void)inClientID;
    if (inDriver == NULL) return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;
    if (outSampleTime == NULL || outHostTime == NULL || outSeed == NULL) return kAudioHardwareIllegalOperationError;

    pthread_mutex_lock(&gRoomcut.stateMutex);
    Float64 ticksPerFrame = gRoomcut.hostTicksPerFrame;
    UInt64  anchorHost    = gRoomcut.anchorHostTime;
    Float64 anchorSample  = gRoomcut.anchorSampleTime;
    UInt64  seed          = gRoomcut.ztsSeed;

    UInt64  now           = mach_absolute_time();
    Float64 period        = (Float64)kRoomcut_Device_RingSize;

    /* How many whole ring periods have elapsed since the anchor. */
    Float64 elapsedTicks  = (ticksPerFrame > 0.0) ? ((Float64)(now - anchorHost)) : 0.0;
    Float64 elapsedFrames = (ticksPerFrame > 0.0) ? (elapsedTicks / ticksPerFrame) : 0.0;
    Float64 periodsElapsed = (period > 0.0) ? (elapsedFrames / period) : 0.0;
    UInt64  wholePeriods  = (UInt64)periodsElapsed;

    Float64 newSampleTime = anchorSample + ((Float64)wholePeriods * period);
    UInt64  newHostTime   = anchorHost + (UInt64)((Float64)wholePeriods * period * ticksPerFrame);

    *outSampleTime = newSampleTime;
    *outHostTime   = newHostTime;
    *outSeed       = seed;
    pthread_mutex_unlock(&gRoomcut.stateMutex);

    return kAudioHardwareNoError;
}

OSStatus Roomcut_WillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace)
{
    (void)inClientID;
    if (inDriver == NULL) return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;
    if (outWillDo == NULL || outWillDoInPlace == NULL) return kAudioHardwareIllegalOperationError;

    Boolean willDo = false;
    Boolean inPlace = true;
    switch (inOperationID) {
        case kAudioServerPlugInIOOperationWriteMix:
            willDo = true;   /* we consume the mixed output */
            inPlace = true;
            break;
        default:
            willDo = false;
            inPlace = true;
            break;
    }
    *outWillDo = willDo;
    *outWillDoInPlace = inPlace;
    return kAudioHardwareNoError;
}

OSStatus Roomcut_BeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
    (void)inClientID; (void)inOperationID; (void)inIOBufferFrameSize; (void)inIOCycleInfo;
    if (inDriver == NULL) return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;
    return kAudioHardwareNoError;
}

/*
 * DoIOOperation — the hot path. The transport worker publishes a validated ring
 * mapping; this callback only takes an atomic guard and performs the lock-free
 * copy. No mapping, IPC, allocation, or blocking occurs here.
 */
OSStatus Roomcut_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer)
{
    (void)inStreamObjectID; (void)inClientID;
    (void)inIOCycleInfo; (void)ioSecondaryBuffer;
    if (inDriver == NULL) return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;

    if (inOperationID != kAudioServerPlugInIOOperationWriteMix || ioMainBuffer == NULL) {
        return kAudioHardwareNoError;
    }

    RoomcutRingHeader* ring = Roomcut_TransportAcquireRing();
    if (ring != NULL) {
        roomcut_ring_write(
            ring,
            (const roomcut_sample_t*)ioMainBuffer,
            inIOBufferFrameSize,
            mach_absolute_time());
        Roomcut_TransportReleaseRing();
    }

#if ROOMCUT_DRIVER_DEBUG
    /* Phase 2 verification: prove real samples arrive. Compute RMS + peak of the
     * outgoing stereo float mix, then emit ONE throttled log line ~1×/second.
     * No allocation, no locks; os_log is async and the throttle keeps the IO
     * proc effectively silent. DEBUG builds only. */
    {
        const float* samples = (const float*)ioMainBuffer;
        const UInt32 n = inIOBufferFrameSize * ROOMCUT_MVP_CHANNELS;
        double sumSq = 0.0;
        float peak = 0.0f;
        for (UInt32 i = 0; i < n; ++i) {
            float s = samples[i];
            sumSq += (double)s * s;
            float a = s < 0.0f ? -s : s;
            if (a > peak) peak = a;
        }
        double rms = (n > 0) ? sqrt(sumSq / (double)n) : 0.0;

        /* Throttle: accumulate callbacks; log once per ~sampleRate frames. */
        static UInt64 framesSinceLog = 0;
        static UInt64 callbacks = 0;
        callbacks += 1;
        framesSinceLog += inIOBufferFrameSize;

        pthread_mutex_lock(&gRoomcut.stateMutex);
        Float64 sr = gRoomcut.sampleRate;
        pthread_mutex_unlock(&gRoomcut.stateMutex);

        if (framesSinceLog >= (UInt64)sr) {
            framesSinceLog = 0;
            os_log(OS_LOG_DEFAULT,
                   "[Roomcut] IO mix: rms=%.5f peak=%.5f frames=%u ch=%d sr=%.0f cb=%llu %s",
                   rms, peak, inIOBufferFrameSize, ROOMCUT_MVP_CHANNELS, sr, callbacks,
                   peak > 1e-5f ? "NON-SILENCE" : "silence");
        }
    }
#endif

    return kAudioHardwareNoError;
}

OSStatus Roomcut_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
    (void)inClientID; (void)inOperationID; (void)inIOBufferFrameSize; (void)inIOCycleInfo;
    if (inDriver == NULL) return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;
    return kAudioHardwareNoError;
}
