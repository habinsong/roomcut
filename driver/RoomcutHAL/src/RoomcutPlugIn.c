/*
 * RoomcutPlugIn.c — AudioServerPlugIn entry point, COM factory, and the
 * AudioServerPlugInDriverInterface function table.
 *
 * coreaudiod discovers the factory named in Info.plist (CFPlugInFactories) and
 * calls it with kAudioServerPlugInTypeUUID. We return a singleton interface;
 * all real work lives in RoomcutProperties.c (object/property model) and
 * RoomcutIO.c (the IO path).
 */
#include "RoomcutDriver.h"
#include "RoomcutTransport.h"

#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <string.h>

/* ---- Global state ---------------------------------------------------------- */

Roomcut_State gRoomcut = {
    .stateMutex        = PTHREAD_MUTEX_INITIALIZER,
    .refCount          = 0,
    .sampleRate        = (Float64)ROOMCUT_SR_48000,
    .volume            = 1.0f,
    .muted             = false,
    .ioRunnerCount     = 0,
    .ioIsRunning       = false,
    .hostTicksPerFrame = 0.0,
    .anchorHostTime    = 0,
    .anchorSampleTime  = 0.0,
    .ztsSeed           = 1
};

AudioServerPlugInHostRef gRoomcut_Host = NULL;

/* Forward declarations for the interface methods defined here. */
static HRESULT  Roomcut_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface);
static ULONG    Roomcut_AddRef(void* inDriver);
static ULONG    Roomcut_Release(void* inDriver);
static OSStatus Roomcut_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost);
static OSStatus Roomcut_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID);
static OSStatus Roomcut_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID);
static OSStatus Roomcut_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus Roomcut_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus Roomcut_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static OSStatus Roomcut_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);

/* The interface vtable. coreaudiod calls through this for every operation. */
static AudioServerPlugInDriverInterface gRoomcut_Interface = {
    NULL,                                       /* _reserved */
    Roomcut_QueryInterface,
    Roomcut_AddRef,
    Roomcut_Release,
    Roomcut_Initialize,
    Roomcut_CreateDevice,
    Roomcut_DestroyDevice,
    Roomcut_AddDeviceClient,
    Roomcut_RemoveDeviceClient,
    Roomcut_PerformDeviceConfigurationChange,
    Roomcut_AbortDeviceConfigurationChange,
    Roomcut_HasProperty,
    Roomcut_IsPropertySettable,
    Roomcut_GetPropertyDataSize,
    Roomcut_GetPropertyData,
    Roomcut_SetPropertyData,
    Roomcut_StartIO,
    Roomcut_StopIO,
    Roomcut_GetZeroTimeStamp,
    Roomcut_WillDoIOOperation,
    Roomcut_BeginIOOperation,
    Roomcut_DoIOOperation,
    Roomcut_EndIOOperation
};

static AudioServerPlugInDriverInterface*  gRoomcut_InterfacePtr = &gRoomcut_Interface;
static AudioServerPlugInDriverRef         gRoomcut_DriverRef    = &gRoomcut_InterfacePtr;

/* ---- Factory --------------------------------------------------------------
 * Named in Info.plist CFPlugInFactories. coreaudiod calls this once with
 * kAudioServerPlugInTypeUUID and expects an AudioServerPlugInDriverInterface**. */
void* Roomcut_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID)
{
    (void)inAllocator;
    if (!CFEqual(inRequestedTypeUUID, kAudioServerPlugInTypeUUID)) {
        return NULL;
    }
    return gRoomcut_DriverRef;
}

/* ---- IUnknown -------------------------------------------------------------- */

static HRESULT Roomcut_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface)
{
    if (inDriver != gRoomcut_DriverRef || outInterface == NULL) {
        return kAudioHardwareIllegalOperationError;
    }

    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(NULL, inUUID);
    if (requested == NULL) {
        return kAudioHardwareIllegalOperationError;
    }

    HRESULT result = E_NOINTERFACE;
    if (CFEqual(requested, IUnknownUUID) ||
        CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID)) {
        pthread_mutex_lock(&gRoomcut.stateMutex);
        gRoomcut.refCount += 1;
        pthread_mutex_unlock(&gRoomcut.stateMutex);
        *outInterface = gRoomcut_DriverRef;
        result = S_OK;
    }

    CFRelease(requested);
    return result;
}

static ULONG Roomcut_AddRef(void* inDriver)
{
    if (inDriver != gRoomcut_DriverRef) {
        return 0;
    }
    pthread_mutex_lock(&gRoomcut.stateMutex);
    if (gRoomcut.refCount < UINT32_MAX) {
        gRoomcut.refCount += 1;
    }
    ULONG rc = gRoomcut.refCount;
    pthread_mutex_unlock(&gRoomcut.stateMutex);
    return rc;
}

static ULONG Roomcut_Release(void* inDriver)
{
    if (inDriver != gRoomcut_DriverRef) {
        return 0;
    }
    pthread_mutex_lock(&gRoomcut.stateMutex);
    if (gRoomcut.refCount > 0) {
        gRoomcut.refCount -= 1;
    }
    ULONG rc = gRoomcut.refCount;
    pthread_mutex_unlock(&gRoomcut.stateMutex);
    /* The interface is a static singleton; nothing to free at refCount 0. */
    return rc;
}

/* ---- Lifecycle ------------------------------------------------------------- */

/* Worker-thread callback (registered before the transport starts): mirror the
 * engine-reported real-output-device rate list onto the published device and,
 * only when it actually changed, tell coreaudiod the available-rate set changed
 * so it re-queries — and re-picks the nominal rate if the current one is no
 * longer offered. count == 0 → fall back to the static advertised set. */
static void Roomcut_OnRatesChanged(const uint32_t* rates, uint32_t count)
{
    if (rates == NULL) count = 0u;
    if (count > ROOMCUT_MAX_RATES) count = ROOMCUT_MAX_RATES;

    bool changed = false;
    pthread_mutex_lock(&gRoomcut.stateMutex);
    if (count != gRoomcut.availableRateCount) {
        changed = true;
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            if (gRoomcut.availableRates[i] != rates[i]) { changed = true; break; }
        }
    }
    if (changed) {
        gRoomcut.availableRateCount = count;
        for (uint32_t i = 0; i < count; ++i) gRoomcut.availableRates[i] = rates[i];
    }
    pthread_mutex_unlock(&gRoomcut.stateMutex);

    if (changed && gRoomcut_Host != NULL) {
        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyAvailableNominalSampleRates,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        gRoomcut_Host->PropertiesChanged(gRoomcut_Host, kObjectID_Device, 1, &addr);
    }

    /* On a rate-LIST change (device connect/switch) drive the nominal rate to the
     * device's HIGHEST supported rate so the ring runs at the device's native
     * rate and the engine renders a bit-exact passthrough (no resampling).
     * Gated on `changed` ONLY: a rate the user picks mid-session (same device →
     * no list change) is left alone, so selecting/lowering the rate sticks.
     * A NominalSampleRate change is honored live (RequestDeviceConfigurationChange
     * → async PerformDeviceConfigurationChange → Roomcut_TransportSetSampleRate
     * → re-HELLO at the new rate). */
    if (changed && count > 0u && gRoomcut_Host != NULL) {
        pthread_mutex_lock(&gRoomcut.stateMutex);
        uint32_t cur = (uint32_t)gRoomcut.sampleRate;
        pthread_mutex_unlock(&gRoomcut.stateMutex);
        uint32_t target = Roomcut_PickNominalRate(cur, rates, count);
        if (target != cur && target != 0u) {
            gRoomcut_Host->RequestDeviceConfigurationChange(
                gRoomcut_Host, kObjectID_Device, (UInt64)target, NULL);
        }
    }
}

static OSStatus Roomcut_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost)
{
    if (inDriver != gRoomcut_DriverRef) {
        return kAudioHardwareBadObjectError;
    }
    gRoomcut_Host = inHost;

    /* Establish the host-ticks-per-frame ratio for the zero-timestamp clock. */
    struct mach_timebase_info tb;
    mach_timebase_info(&tb);
    Float64 hostTicksPerSecond = (Float64)tb.denom / (Float64)tb.numer * 1.0e9;

    pthread_mutex_lock(&gRoomcut.stateMutex);
    gRoomcut.hostTicksPerFrame = hostTicksPerSecond / gRoomcut.sampleRate;
    UInt32 sampleRate = (UInt32)gRoomcut.sampleRate;
    pthread_mutex_unlock(&gRoomcut.stateMutex);

    Roomcut_TransportSetRatesChangedCallback(Roomcut_OnRatesChanged);
    Roomcut_TransportStart(sampleRate);
    return kAudioHardwareNoError;
}

/* Roomcut publishes a single fixed device; it never creates devices on demand. */
static OSStatus Roomcut_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID)
{
    (void)inDriver; (void)inDescription; (void)inClientInfo; (void)outDeviceObjectID;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus Roomcut_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID)
{
    (void)inDriver; (void)inDeviceObjectID;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus Roomcut_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo)
{
    (void)inClientInfo;
    if (inDriver != gRoomcut_DriverRef) {
        return kAudioHardwareBadObjectError;
    }
    if (inDeviceObjectID != kObjectID_Device) {
        return kAudioHardwareBadObjectError;
    }
    return kAudioHardwareNoError;
}

static OSStatus Roomcut_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo)
{
    (void)inClientInfo;
    if (inDriver != gRoomcut_DriverRef) {
        return kAudioHardwareBadObjectError;
    }
    if (inDeviceObjectID != kObjectID_Device) {
        return kAudioHardwareBadObjectError;
    }
    return kAudioHardwareNoError;
}

static OSStatus Roomcut_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{
    (void)inChangeInfo;
    if (inDriver != gRoomcut_DriverRef) {
        return kAudioHardwareBadObjectError;
    }
    if (inDeviceObjectID != kObjectID_Device) {
        return kAudioHardwareBadObjectError;
    }

    /* inChangeAction carries a requested sample rate (see SetPropertyData for
     * kAudioDevicePropertyNominalSampleRate). Apply it under the lock. */
    Float64 newRate = (Float64)inChangeAction;
    if (roomcut_sr_supported((uint32_t)newRate)) {
        struct mach_timebase_info tb;
        mach_timebase_info(&tb);
        Float64 hostTicksPerSecond = (Float64)tb.denom / (Float64)tb.numer * 1.0e9;

        pthread_mutex_lock(&gRoomcut.stateMutex);
        gRoomcut.sampleRate = newRate;
        gRoomcut.hostTicksPerFrame = hostTicksPerSecond / newRate;
        pthread_mutex_unlock(&gRoomcut.stateMutex);
        Roomcut_TransportSetSampleRate((UInt32)newRate);
        return kAudioHardwareNoError;
    }
    return kAudioHardwareIllegalOperationError;
}

static OSStatus Roomcut_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{
    (void)inChangeAction; (void)inChangeInfo;
    if (inDriver != gRoomcut_DriverRef) {
        return kAudioHardwareBadObjectError;
    }
    if (inDeviceObjectID != kObjectID_Device) {
        return kAudioHardwareBadObjectError;
    }
    return kAudioHardwareNoError;
}
