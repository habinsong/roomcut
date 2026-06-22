/*
 * RoomcutDriver.h — internal shared declarations for the Roomcut Audio Server
 * Plug-in. Not a public header; only the driver's own .c files include it.
 *
 * Roomcut.driver is implemented directly against the public AudioServerPlugIn
 * C API (no BlackHole/libASPL/NullAudio source is vendored — see docs/02).
 *
 * Publishes a stereo float32 output device ("Roomcut Output") at 44.1/48 kHz
 * with master volume + mute controls and forwards its mix to the engine's
 * shared-memory ring.
 */
#ifndef ROOMCUT_DRIVER_H
#define ROOMCUT_DRIVER_H

#include <CoreAudio/AudioServerPlugIn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "roomcut_audio_format.h"

/* ---- Identity -------------------------------------------------------------
 * Keep these in sync with Info.plist and docs/02-driver.md. */
#define kRoomcut_BundleID            "com.roomcut.driver"
#define kRoomcut_Device_Name         "Roomcut Output"
#define kRoomcut_Manufacturer_Name   "Roomcut"
#define kRoomcut_Device_UID          "RoomcutOutput:com.roomcut"
#define kRoomcut_Device_ModelUID     "RoomcutOutput:Model"
#define kRoomcut_Box_UID             "RoomcutBox:com.roomcut"

/* Mach service the engine publishes and the driver connects to. */
#define kRoomcut_MachServiceName     "com.roomcut.engine"

/* ---- Object IDs -----------------------------------------------------------
 * Static object graph. The plug-in object is always kAudioObjectPlugInObject. */
enum {
    kObjectID_PlugIn               = kAudioObjectPlugInObject, /* 1 */
    kObjectID_Box                  = 2,
    kObjectID_Device               = 3,
    kObjectID_Stream_Output        = 4,
    kObjectID_Volume_Output_Master = 5,
    kObjectID_Mute_Output_Master   = 6
};

/* Ring buffer / safety: number of frames the device claims as its IO ring. */
#define kRoomcut_Device_RingSize    8192u

/* ---- Global state ---------------------------------------------------------
 * A single static device instance. Guarded by gPlugIn_StateMutex for anything
 * touched from both the control path and the (real-time) IO path; the IO path
 * itself only reads immutable values and the atomically-updated host clock. */
typedef struct {
    pthread_mutex_t  stateMutex;       /* guards mutable control-path fields   */

    UInt32           refCount;         /* COM ref count                        */

    /* Negotiated/active format. */
    Float64          sampleRate;       /* 44.1/48/88.2/96/176.4/192 kHz        */

    /* Sample rates the device advertises. Populated from the REAL output
     * device's supported rates (forwarded by the engine over HELLO) so
     * coreaudiod settles on a rate the device runs natively. availableRateCount
     * == 0 means none reported yet → advertise the full static set instead. */
    uint32_t         availableRateCount;
    uint32_t         availableRates[ROOMCUT_MAX_RATES];

    /* Controls. */
    Float32          volume;           /* 0..1 scalar                          */
    bool             muted;

    /* IO run state. */
    UInt64           ioRunnerCount;    /* number of started IO clients         */
    bool             ioIsRunning;
    Float64          hostTicksPerFrame;/* mach host ticks per audio frame      */
    UInt64           anchorHostTime;   /* host time at IO start                */
    Float64          anchorSampleTime; /* sample time at IO start              */
    UInt64           ztsSeed;          /* zero-timestamp seed                  */
} Roomcut_State;

extern Roomcut_State gRoomcut;

/* Host interface handed to us in Initialize(); used to notify property changes. */
extern AudioServerPlugInHostRef gRoomcut_Host;

/* ---- Cross-file entry points ---------------------------------------------- */

/* Factory named in Info.plist CFPlugInFactories. Returns the COM interface. */
void* Roomcut_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID);

/* Property dispatch (RoomcutProperties.c). */
Boolean   Roomcut_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress);
OSStatus  Roomcut_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
OSStatus  Roomcut_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
OSStatus  Roomcut_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
OSStatus  Roomcut_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);

/* IO dispatch (RoomcutIO.c). */
OSStatus  Roomcut_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
OSStatus  Roomcut_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
OSStatus  Roomcut_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed);
OSStatus  Roomcut_WillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace);
OSStatus  Roomcut_BeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);
OSStatus  Roomcut_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer);
OSStatus  Roomcut_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);

#endif /* ROOMCUT_DRIVER_H */
