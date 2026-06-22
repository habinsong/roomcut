/*
 * RoomcutProperties.c — the object & property model for Roomcut Output.
 *
 * Implements HasProperty / IsPropertySettable / GetPropertyDataSize /
 * GetPropertyData / SetPropertyData for the static object graph:
 *
 *   kObjectID_PlugIn  → kObjectID_Box → kObjectID_Device
 *                                        ├─ kObjectID_Stream_Output
 *                                        ├─ kObjectID_Volume_Output_Master
 *                                        └─ kObjectID_Mute_Output_Master
 *
 * Phase 1: stereo float32, 44.1/48 kHz, master volume + mute. This is the
 * spec-of-record set of selectors a HAL output device must answer; we return
 * them directly (NullAudio is read as a spec, not copied — docs/02).
 */
#include "RoomcutDriver.h"

#include <CoreFoundation/CoreFoundation.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

/* ---- small helpers --------------------------------------------------------- */

static const AudioChannelLayoutTag kRoomcut_ChannelLayoutTag = kAudioChannelLayoutTag_Stereo;

/* Map a 0..1 scalar volume to a dB value and back, for the volume control. */
static const Float32 kRoomcut_MinVolumeDB = -64.0f;
static const Float32 kRoomcut_MaxVolumeDB =   0.0f;

static Float32 Roomcut_ScalarToDB(Float32 scalar)
{
    if (scalar <= 0.0f) return kRoomcut_MinVolumeDB;
    Float32 db = 20.0f * log10f(scalar);
    if (db < kRoomcut_MinVolumeDB) db = kRoomcut_MinVolumeDB;
    if (db > kRoomcut_MaxVolumeDB) db = kRoomcut_MaxVolumeDB;
    return db;
}

static Float32 Roomcut_DBToScalar(Float32 db)
{
    if (db <= kRoomcut_MinVolumeDB) return 0.0f;
    if (db >= kRoomcut_MaxVolumeDB) return 1.0f;
    return powf(10.0f, db / 20.0f);
}

/* Fill an AudioStreamBasicDescription for our canonical float32 stereo format. */
static void Roomcut_FillASBD(AudioStreamBasicDescription* asbd, Float64 sampleRate)
{
    asbd->mSampleRate       = sampleRate;
    asbd->mFormatID         = kAudioFormatLinearPCM;
    asbd->mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    asbd->mBytesPerPacket   = ROOMCUT_BYTES_PER_SAMPLE * ROOMCUT_MVP_CHANNELS;
    asbd->mFramesPerPacket  = 1;
    asbd->mBytesPerFrame    = ROOMCUT_BYTES_PER_SAMPLE * ROOMCUT_MVP_CHANNELS;
    asbd->mChannelsPerFrame = ROOMCUT_MVP_CHANNELS;
    asbd->mBitsPerChannel   = ROOMCUT_BYTES_PER_SAMPLE * 8;
    asbd->mReserved         = 0;
}

/* ---------------------------------------------------------------------------
 * Per-object property tables. Each object answers HasProperty and the four
 * data calls. We dispatch on inObjectID then on inAddress->mSelector.
 * ------------------------------------------------------------------------- */

/* ===== PlugIn object ===== */

static Boolean PlugIn_Has(const AudioObjectPropertyAddress* a)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyBoxList:
        case kAudioPlugInPropertyTranslateUIDToBox:
        case kAudioPlugInPropertyDeviceList:
        case kAudioPlugInPropertyTranslateUIDToDevice:
        case kAudioPlugInPropertyResourceBundle:
            return true;
        default:
            return false;
    }
}

static OSStatus PlugIn_GetSize(const AudioObjectPropertyAddress* a, UInt32* outSize)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:        *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:            *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:            *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyManufacturer:     *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:     *outSize = 1 * sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioPlugInPropertyBoxList:          *outSize = 1 * sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioPlugInPropertyTranslateUIDToBox:*outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioPlugInPropertyDeviceList:       *outSize = 1 * sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioPlugInPropertyTranslateUIDToDevice: *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioPlugInPropertyResourceBundle:   *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        default: return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus PlugIn_Get(const AudioObjectPropertyAddress* a, UInt32 inQualDataSize, const void* inQualData, UInt32 inDataSize, UInt32* outSize, void* outData)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
            *((AudioClassID*)outData) = kAudioObjectClassID;
            *outSize = sizeof(AudioClassID);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:
            *((AudioClassID*)outData) = kAudioPlugInClassID;
            *outSize = sizeof(AudioClassID);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
            *((AudioObjectID*)outData) = kAudioObjectUnknown;
            *outSize = sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyManufacturer:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Manufacturer_Name);
            *outSize = sizeof(CFStringRef);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyBoxList: {
            UInt32 n = inDataSize / sizeof(AudioObjectID);
            AudioObjectID* ids = (AudioObjectID*)outData;
            UInt32 written = 0;
            if (n >= 1) { ids[0] = kObjectID_Box; written = 1; }
            *outSize = written * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioPlugInPropertyDeviceList: {
            UInt32 n = inDataSize / sizeof(AudioObjectID);
            AudioObjectID* ids = (AudioObjectID*)outData;
            UInt32 written = 0;
            if (n >= 1) { ids[0] = kObjectID_Device; written = 1; }
            *outSize = written * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioPlugInPropertyTranslateUIDToBox: {
            if (inQualDataSize < sizeof(CFStringRef) || inQualData == NULL) {
                return kAudioHardwareIllegalOperationError;
            }
            CFStringRef uid = *((const CFStringRef*)inQualData);
            *((AudioObjectID*)outData) = CFEqual(uid, CFSTR(kRoomcut_Box_UID)) ? kObjectID_Box : kAudioObjectUnknown;
            *outSize = sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioPlugInPropertyTranslateUIDToDevice: {
            if (inQualDataSize < sizeof(CFStringRef) || inQualData == NULL) {
                return kAudioHardwareIllegalOperationError;
            }
            CFStringRef uid = *((const CFStringRef*)inQualData);
            *((AudioObjectID*)outData) = CFEqual(uid, CFSTR(kRoomcut_Device_UID)) ? kObjectID_Device : kAudioObjectUnknown;
            *outSize = sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioPlugInPropertyResourceBundle:
            *((CFStringRef*)outData) = CFSTR("");
            *outSize = sizeof(CFStringRef);
            return kAudioHardwareNoError;
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

/* ===== Box object ===== */

static Boolean Box_Has(const AudioObjectPropertyAddress* a)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyModelName:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioObjectPropertyIdentify:
        case kAudioObjectPropertySerialNumber:
        case kAudioObjectPropertyFirmwareVersion:
        case kAudioBoxPropertyBoxUID:
        case kAudioBoxPropertyTransportType:
        case kAudioBoxPropertyHasAudio:
        case kAudioBoxPropertyHasVideo:
        case kAudioBoxPropertyHasMIDI:
        case kAudioBoxPropertyIsProtected:
        case kAudioBoxPropertyAcquired:
        case kAudioBoxPropertyAcquisitionFailed:
        case kAudioBoxPropertyDeviceList:
            return true;
        default:
            return false;
    }
}

static OSStatus Box_GetSize(const AudioObjectPropertyAddress* a, UInt32* outSize)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:        *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:            *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:            *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyName:             *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyModelName:        *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyManufacturer:     *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:     *outSize = 0; return kAudioHardwareNoError;
        case kAudioObjectPropertyIdentify:         *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioObjectPropertySerialNumber:     *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyFirmwareVersion:  *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioBoxPropertyBoxUID:              *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioBoxPropertyTransportType:       *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyHasAudio:            *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyHasVideo:            *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyHasMIDI:             *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyIsProtected:         *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyAcquired:            *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyAcquisitionFailed:   *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyDeviceList:          *outSize = 1 * sizeof(AudioObjectID); return kAudioHardwareNoError;
        default: return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus Box_Get(const AudioObjectPropertyAddress* a, UInt32 inDataSize, UInt32* outSize, void* outData)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
            *((AudioClassID*)outData) = kAudioObjectClassID; *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:
            *((AudioClassID*)outData) = kAudioBoxClassID; *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
            *((AudioObjectID*)outData) = kObjectID_PlugIn; *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyName:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Device_Name); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyModelName:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Device_Name " Model"); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyManufacturer:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Manufacturer_Name); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:
            *outSize = 0; return kAudioHardwareNoError;
        case kAudioObjectPropertyIdentify:
            *((UInt32*)outData) = 0; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioObjectPropertySerialNumber:
            *((CFStringRef*)outData) = CFSTR("0001"); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyFirmwareVersion:
            *((CFStringRef*)outData) = CFSTR("1.0"); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioBoxPropertyBoxUID:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Box_UID); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioBoxPropertyTransportType:
            *((UInt32*)outData) = kAudioDeviceTransportTypeVirtual; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyHasAudio:
            *((UInt32*)outData) = 1; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyHasVideo:
        case kAudioBoxPropertyHasMIDI:
        case kAudioBoxPropertyIsProtected:
        case kAudioBoxPropertyAcquisitionFailed:
            *((UInt32*)outData) = 0; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyAcquired:
            *((UInt32*)outData) = 1; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioBoxPropertyDeviceList: {
            UInt32 n = inDataSize / sizeof(AudioObjectID);
            AudioObjectID* ids = (AudioObjectID*)outData;
            UInt32 written = 0;
            if (n >= 1) { ids[0] = kObjectID_Device; written = 1; }
            *outSize = written * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

/* ===== Device object ===== */

static Boolean Device_Has(const AudioObjectPropertyAddress* a)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyRelatedDevices:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertyStreams:
        case kAudioObjectPropertyControlList:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDevicePropertyAvailableNominalSampleRates:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyIcon:
        case kAudioDevicePropertyPreferredChannelsForStereo:
        case kAudioDevicePropertyPreferredChannelLayout:
            return true;
        default:
            return false;
    }
}

static OSStatus Device_GetSize(const AudioObjectPropertyAddress* a, UInt32* outSize)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:        *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:            *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:            *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyName:             *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyManufacturer:     *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:     *outSize = 3 * sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioDevicePropertyDeviceUID:        *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioDevicePropertyModelUID:         *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioDevicePropertyTransportType:    *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyRelatedDevices:   *outSize = 1 * sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioDevicePropertyClockDomain:      *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyDeviceIsAlive:    *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyDeviceIsRunning:  *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:        *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:  *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyLatency:          *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyStreams:          *outSize = 1 * sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyControlList:      *outSize = 2 * sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioDevicePropertySafetyOffset:     *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyNominalSampleRate:*outSize = sizeof(Float64); return kAudioHardwareNoError;
        case kAudioDevicePropertyAvailableNominalSampleRates: {
            pthread_mutex_lock(&gRoomcut.stateMutex);
            UInt32 cnt = gRoomcut.availableRateCount;
            pthread_mutex_unlock(&gRoomcut.stateMutex);
            if (cnt == 0u || cnt > ROOMCUT_MAX_RATES) cnt = ROOMCUT_SR_COUNT;
            *outSize = cnt * sizeof(AudioValueRange); return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyIsHidden:         *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyZeroTimeStampPeriod: *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyIcon:             *outSize = sizeof(CFURLRef); return kAudioHardwareNoError;
        case kAudioDevicePropertyPreferredChannelsForStereo: *outSize = 2 * sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyPreferredChannelLayout: *outSize = offsetof(AudioChannelLayout, mChannelDescriptions); return kAudioHardwareNoError;
        default: return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus Device_Get(const AudioObjectPropertyAddress* a, UInt32 inDataSize, UInt32* outSize, void* outData)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
            *((AudioClassID*)outData) = kAudioObjectClassID; *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:
            *((AudioClassID*)outData) = kAudioDeviceClassID; *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
            *((AudioObjectID*)outData) = kObjectID_PlugIn; *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyName:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Device_Name); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyManufacturer:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Manufacturer_Name); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects: {
            UInt32 n = inDataSize / sizeof(AudioObjectID);
            AudioObjectID* ids = (AudioObjectID*)outData;
            UInt32 w = 0;
            if (n > w) ids[w++] = kObjectID_Stream_Output;
            if (n > w) ids[w++] = kObjectID_Volume_Output_Master;
            if (n > w) ids[w++] = kObjectID_Mute_Output_Master;
            *outSize = w * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyDeviceUID:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Device_UID); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioDevicePropertyModelUID:
            *((CFStringRef*)outData) = CFSTR(kRoomcut_Device_ModelUID); *outSize = sizeof(CFStringRef); return kAudioHardwareNoError;
        case kAudioDevicePropertyTransportType:
            *((UInt32*)outData) = kAudioDeviceTransportTypeVirtual; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyRelatedDevices: {
            UInt32 n = inDataSize / sizeof(AudioObjectID);
            AudioObjectID* ids = (AudioObjectID*)outData;
            UInt32 w = 0;
            if (n > w) ids[w++] = kObjectID_Device;
            *outSize = w * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyClockDomain:
            *((UInt32*)outData) = 0; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyDeviceIsAlive:
            *((UInt32*)outData) = 1; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyDeviceIsRunning: {
            pthread_mutex_lock(&gRoomcut.stateMutex);
            UInt32 running = gRoomcut.ioIsRunning ? 1 : 0;
            pthread_mutex_unlock(&gRoomcut.stateMutex);
            *((UInt32*)outData) = running; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            *((UInt32*)outData) = 1; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
            *((UInt32*)outData) = 1; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyLatency:
            *((UInt32*)outData) = 0; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyStreams: {
            UInt32 n = inDataSize / sizeof(AudioObjectID);
            AudioObjectID* ids = (AudioObjectID*)outData;
            UInt32 w = 0;
            if (n > w) ids[w++] = kObjectID_Stream_Output;
            *outSize = w * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioObjectPropertyControlList: {
            UInt32 n = inDataSize / sizeof(AudioObjectID);
            AudioObjectID* ids = (AudioObjectID*)outData;
            UInt32 w = 0;
            if (n > w) ids[w++] = kObjectID_Volume_Output_Master;
            if (n > w) ids[w++] = kObjectID_Mute_Output_Master;
            *outSize = w * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertySafetyOffset:
            *((UInt32*)outData) = 0; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyNominalSampleRate: {
            pthread_mutex_lock(&gRoomcut.stateMutex);
            Float64 sr = gRoomcut.sampleRate;
            pthread_mutex_unlock(&gRoomcut.stateMutex);
            *((Float64*)outData) = sr; *outSize = sizeof(Float64); return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyAvailableNominalSampleRates: {
            UInt32 n = inDataSize / sizeof(AudioValueRange);
            AudioValueRange* r = (AudioValueRange*)outData;
            static const Float64 kStatic[ROOMCUT_SR_COUNT] = {
                ROOMCUT_SR_44100, ROOMCUT_SR_48000, ROOMCUT_SR_88200,
                ROOMCUT_SR_96000, ROOMCUT_SR_176400, ROOMCUT_SR_192000
            };
            /* Advertise the real output device's rates when the engine has
             * forwarded them; otherwise the full static set. Never empty. */
            Float64 rates[ROOMCUT_MAX_RATES];
            UInt32 count;
            pthread_mutex_lock(&gRoomcut.stateMutex);
            count = gRoomcut.availableRateCount;
            if (count > 0 && count <= ROOMCUT_MAX_RATES) {
                for (UInt32 i = 0; i < count; ++i) rates[i] = (Float64)gRoomcut.availableRates[i];
            } else {
                count = ROOMCUT_SR_COUNT;
                for (UInt32 i = 0; i < count; ++i) rates[i] = kStatic[i];
            }
            pthread_mutex_unlock(&gRoomcut.stateMutex);
            UInt32 w = 0;
            for (UInt32 i = 0; i < count && n > w; ++i) {
                r[w].mMinimum = rates[i]; r[w].mMaximum = rates[i]; w++;
            }
            *outSize = w * sizeof(AudioValueRange);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyIsHidden:
            *((UInt32*)outData) = 0; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyZeroTimeStampPeriod:
            *((UInt32*)outData) = kRoomcut_Device_RingSize; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioDevicePropertyIcon:
            *((CFURLRef*)outData) = NULL; *outSize = sizeof(CFURLRef); return kAudioHardwareNoError;
        case kAudioDevicePropertyPreferredChannelsForStereo: {
            UInt32* ch = (UInt32*)outData;
            ch[0] = 1; ch[1] = 2;
            *outSize = 2 * sizeof(UInt32);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyPreferredChannelLayout: {
            AudioChannelLayout* layout = (AudioChannelLayout*)outData;
            memset(layout, 0, offsetof(AudioChannelLayout, mChannelDescriptions));
            layout->mChannelLayoutTag = kRoomcut_ChannelLayoutTag;
            *outSize = offsetof(AudioChannelLayout, mChannelDescriptions);
            return kAudioHardwareNoError;
        }
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

/* ===== Stream object (output) ===== */

static Boolean Stream_Has(const AudioObjectPropertyAddress* a)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency:
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            return true;
        default:
            return false;
    }
}

static OSStatus Stream_GetSize(const AudioObjectPropertyAddress* a, UInt32* outSize)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:        *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:            *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:            *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:     *outSize = 0; return kAudioHardwareNoError;
        case kAudioStreamPropertyIsActive:         *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyDirection:        *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyTerminalType:     *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyStartingChannel:  *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyLatency:          *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:   *outSize = sizeof(AudioStreamBasicDescription); return kAudioHardwareNoError;
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats: {
            pthread_mutex_lock(&gRoomcut.stateMutex);
            UInt32 cnt = gRoomcut.availableRateCount;
            pthread_mutex_unlock(&gRoomcut.stateMutex);
            if (cnt == 0u || cnt > ROOMCUT_MAX_RATES) cnt = ROOMCUT_SR_COUNT;
            *outSize = cnt * sizeof(AudioStreamRangedDescription); return kAudioHardwareNoError;
        }
        default: return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus Stream_Get(const AudioObjectPropertyAddress* a, UInt32 inDataSize, UInt32* outSize, void* outData)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
            *((AudioClassID*)outData) = kAudioObjectClassID; *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:
            *((AudioClassID*)outData) = kAudioStreamClassID; *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
            *((AudioObjectID*)outData) = kObjectID_Device; *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:
            *outSize = 0; return kAudioHardwareNoError;
        case kAudioStreamPropertyIsActive:
            *((UInt32*)outData) = 1; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyDirection:
            *((UInt32*)outData) = 0; /* 0 = output */ *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyTerminalType:
            *((UInt32*)outData) = kAudioStreamTerminalTypeSpeaker; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyStartingChannel:
            *((UInt32*)outData) = 1; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyLatency:
            *((UInt32*)outData) = 0; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat: {
            pthread_mutex_lock(&gRoomcut.stateMutex);
            Float64 sr = gRoomcut.sampleRate;
            pthread_mutex_unlock(&gRoomcut.stateMutex);
            Roomcut_FillASBD((AudioStreamBasicDescription*)outData, sr);
            *outSize = sizeof(AudioStreamBasicDescription);
            return kAudioHardwareNoError;
        }
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats: {
            UInt32 n = inDataSize / sizeof(AudioStreamRangedDescription);
            AudioStreamRangedDescription* descs = (AudioStreamRangedDescription*)outData;
            /* Offer a physical/virtual format at every rate the device actually
             * supports (engine-forwarded), so the device can run at hi-res (e.g.
             * 384 kHz), not just the static fallback set. */
            static const Float64 kStatic[ROOMCUT_SR_COUNT] = {
                ROOMCUT_SR_44100, ROOMCUT_SR_48000, ROOMCUT_SR_88200,
                ROOMCUT_SR_96000, ROOMCUT_SR_176400, ROOMCUT_SR_192000
            };
            Float64 rates[ROOMCUT_MAX_RATES];
            UInt32 count;
            pthread_mutex_lock(&gRoomcut.stateMutex);
            count = gRoomcut.availableRateCount;
            if (count > 0u && count <= ROOMCUT_MAX_RATES) {
                for (UInt32 i = 0; i < count; ++i) rates[i] = (Float64)gRoomcut.availableRates[i];
            } else {
                count = ROOMCUT_SR_COUNT;
                for (UInt32 i = 0; i < count; ++i) rates[i] = kStatic[i];
            }
            pthread_mutex_unlock(&gRoomcut.stateMutex);
            UInt32 w = 0;
            for (UInt32 i = 0; i < count && n > w; ++i) {
                Roomcut_FillASBD(&descs[w].mFormat, rates[i]);
                descs[w].mSampleRateRange.mMinimum = rates[i];
                descs[w].mSampleRateRange.mMaximum = rates[i];
                w++;
            }
            *outSize = w * sizeof(AudioStreamRangedDescription);
            return kAudioHardwareNoError;
        }
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

/* ===== Control objects (master volume + master mute) ===== */

static Boolean Control_Has(AudioObjectID objID, const AudioObjectPropertyAddress* a)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioControlPropertyScope:
        case kAudioControlPropertyElement:
            return true;
        default:
            break;
    }
    if (objID == kObjectID_Volume_Output_Master) {
        switch (a->mSelector) {
            case kAudioLevelControlPropertyScalarValue:
            case kAudioLevelControlPropertyDecibelValue:
            case kAudioLevelControlPropertyDecibelRange:
            case kAudioLevelControlPropertyConvertScalarToDecibels:
            case kAudioLevelControlPropertyConvertDecibelsToScalar:
                return true;
            default: return false;
        }
    } else { /* mute */
        return a->mSelector == kAudioBooleanControlPropertyValue;
    }
}

static OSStatus Control_GetSize(AudioObjectID objID, const AudioObjectPropertyAddress* a, UInt32* outSize)
{
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:   *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:       *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:       *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:*outSize = 0; return kAudioHardwareNoError;
        case kAudioControlPropertyScope:      *outSize = sizeof(AudioObjectPropertyScope); return kAudioHardwareNoError;
        case kAudioControlPropertyElement:    *outSize = sizeof(AudioObjectPropertyElement); return kAudioHardwareNoError;
        default: break;
    }
    if (objID == kObjectID_Volume_Output_Master) {
        switch (a->mSelector) {
            case kAudioLevelControlPropertyScalarValue:  *outSize = sizeof(Float32); return kAudioHardwareNoError;
            case kAudioLevelControlPropertyDecibelValue: *outSize = sizeof(Float32); return kAudioHardwareNoError;
            case kAudioLevelControlPropertyDecibelRange: *outSize = sizeof(AudioValueRange); return kAudioHardwareNoError;
            case kAudioLevelControlPropertyConvertScalarToDecibels: *outSize = sizeof(Float32); return kAudioHardwareNoError;
            case kAudioLevelControlPropertyConvertDecibelsToScalar: *outSize = sizeof(Float32); return kAudioHardwareNoError;
            default: return kAudioHardwareUnknownPropertyError;
        }
    } else {
        if (a->mSelector == kAudioBooleanControlPropertyValue) { *outSize = sizeof(UInt32); return kAudioHardwareNoError; }
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus Control_Get(AudioObjectID objID, const AudioObjectPropertyAddress* a, UInt32* outSize, void* outData)
{
    const bool isVolume = (objID == kObjectID_Volume_Output_Master);

    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
            *((AudioClassID*)outData) = isVolume ? kAudioLevelControlClassID : kAudioBooleanControlClassID;
            *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyClass:
            *((AudioClassID*)outData) = isVolume ? kAudioVolumeControlClassID : kAudioMuteControlClassID;
            *outSize = sizeof(AudioClassID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
            *((AudioObjectID*)outData) = kObjectID_Device; *outSize = sizeof(AudioObjectID); return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:
            *outSize = 0; return kAudioHardwareNoError;
        case kAudioControlPropertyScope:
            *((AudioObjectPropertyScope*)outData) = kAudioObjectPropertyScopeOutput;
            *outSize = sizeof(AudioObjectPropertyScope); return kAudioHardwareNoError;
        case kAudioControlPropertyElement:
            *((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
            *outSize = sizeof(AudioObjectPropertyElement); return kAudioHardwareNoError;
        default: break;
    }

    if (isVolume) {
        pthread_mutex_lock(&gRoomcut.stateMutex);
        Float32 vol = gRoomcut.volume;
        pthread_mutex_unlock(&gRoomcut.stateMutex);
        switch (a->mSelector) {
            case kAudioLevelControlPropertyScalarValue:
                *((Float32*)outData) = vol; *outSize = sizeof(Float32); return kAudioHardwareNoError;
            case kAudioLevelControlPropertyDecibelValue:
                *((Float32*)outData) = Roomcut_ScalarToDB(vol); *outSize = sizeof(Float32); return kAudioHardwareNoError;
            case kAudioLevelControlPropertyDecibelRange: {
                AudioValueRange* r = (AudioValueRange*)outData;
                r->mMinimum = kRoomcut_MinVolumeDB; r->mMaximum = kRoomcut_MaxVolumeDB;
                *outSize = sizeof(AudioValueRange); return kAudioHardwareNoError;
            }
            case kAudioLevelControlPropertyConvertScalarToDecibels:
                *((Float32*)outData) = Roomcut_ScalarToDB(*((Float32*)outData)); *outSize = sizeof(Float32); return kAudioHardwareNoError;
            case kAudioLevelControlPropertyConvertDecibelsToScalar:
                *((Float32*)outData) = Roomcut_DBToScalar(*((Float32*)outData)); *outSize = sizeof(Float32); return kAudioHardwareNoError;
            default: return kAudioHardwareUnknownPropertyError;
        }
    } else {
        if (a->mSelector == kAudioBooleanControlPropertyValue) {
            pthread_mutex_lock(&gRoomcut.stateMutex);
            UInt32 muted = gRoomcut.muted ? 1 : 0;
            pthread_mutex_unlock(&gRoomcut.stateMutex);
            *((UInt32*)outData) = muted; *outSize = sizeof(UInt32); return kAudioHardwareNoError;
        }
        return kAudioHardwareUnknownPropertyError;
    }
}

/* ---------------------------------------------------------------------------
 * Public dispatch: route by object ID, then delegate to the per-object tables.
 * ------------------------------------------------------------------------- */

Boolean Roomcut_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress)
{
    (void)inDriver; (void)inClientPID;
    if (inAddress == NULL) return false;
    switch (inObjectID) {
        case kObjectID_PlugIn:        return PlugIn_Has(inAddress);
        case kObjectID_Box:           return Box_Has(inAddress);
        case kObjectID_Device:        return Device_Has(inAddress);
        case kObjectID_Stream_Output: return Stream_Has(inAddress);
        case kObjectID_Volume_Output_Master:
        case kObjectID_Mute_Output_Master: return Control_Has(inObjectID, inAddress);
        default: return false;
    }
}

OSStatus Roomcut_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
    (void)inDriver; (void)inClientPID;
    if (inAddress == NULL || outIsSettable == NULL) return kAudioHardwareIllegalOperationError;

    if (!Roomcut_HasProperty(inDriver, inObjectID, inClientPID, inAddress)) {
        return kAudioHardwareUnknownPropertyError;
    }

    Boolean settable = false;
    switch (inObjectID) {
        case kObjectID_Device:
            settable = (inAddress->mSelector == kAudioDevicePropertyNominalSampleRate);
            break;
        case kObjectID_Volume_Output_Master:
            settable = (inAddress->mSelector == kAudioLevelControlPropertyScalarValue ||
                        inAddress->mSelector == kAudioLevelControlPropertyDecibelValue);
            break;
        case kObjectID_Mute_Output_Master:
            settable = (inAddress->mSelector == kAudioBooleanControlPropertyValue);
            break;
        default:
            settable = false;
            break;
    }
    *outIsSettable = settable;
    return kAudioHardwareNoError;
}

OSStatus Roomcut_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
    (void)inDriver; (void)inClientPID; (void)inQualifierDataSize; (void)inQualifierData;
    if (inAddress == NULL || outDataSize == NULL) return kAudioHardwareIllegalOperationError;
    switch (inObjectID) {
        case kObjectID_PlugIn:        return PlugIn_GetSize(inAddress, outDataSize);
        case kObjectID_Box:           return Box_GetSize(inAddress, outDataSize);
        case kObjectID_Device:        return Device_GetSize(inAddress, outDataSize);
        case kObjectID_Stream_Output: return Stream_GetSize(inAddress, outDataSize);
        case kObjectID_Volume_Output_Master:
        case kObjectID_Mute_Output_Master: return Control_GetSize(inObjectID, inAddress, outDataSize);
        default: return kAudioHardwareBadObjectError;
    }
}

OSStatus Roomcut_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
    (void)inDriver; (void)inClientPID;
    if (inAddress == NULL || outDataSize == NULL || outData == NULL) return kAudioHardwareIllegalOperationError;
    switch (inObjectID) {
        case kObjectID_PlugIn:        return PlugIn_Get(inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
        case kObjectID_Box:           return Box_Get(inAddress, inDataSize, outDataSize, outData);
        case kObjectID_Device:        return Device_Get(inAddress, inDataSize, outDataSize, outData);
        case kObjectID_Stream_Output: return Stream_Get(inAddress, inDataSize, outDataSize, outData);
        case kObjectID_Volume_Output_Master:
        case kObjectID_Mute_Output_Master: return Control_Get(inObjectID, inAddress, outDataSize, outData);
        default: return kAudioHardwareBadObjectError;
    }
}

OSStatus Roomcut_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
    (void)inClientPID; (void)inQualifierDataSize; (void)inQualifierData;
    if (inAddress == NULL || inData == NULL) return kAudioHardwareIllegalOperationError;

    switch (inObjectID) {
        case kObjectID_Device:
            if (inAddress->mSelector == kAudioDevicePropertyNominalSampleRate) {
                if (inDataSize < sizeof(Float64)) return kAudioHardwareBadPropertySizeError;
                Float64 requested = *((const Float64*)inData);
                if (!roomcut_sr_supported((uint32_t)requested)) {
                    return kAudioHardwareIllegalOperationError;
                }
                /* The actual switch happens asynchronously via the host so IO can
                 * quiesce; encode the rate in the change action. */
                if (gRoomcut_Host != NULL) {
                    gRoomcut_Host->RequestDeviceConfigurationChange(gRoomcut_Host, kObjectID_Device, (UInt64)requested, NULL);
                }
                return kAudioHardwareNoError;
            }
            return kAudioHardwareUnknownPropertyError;

        case kObjectID_Volume_Output_Master: {
            Float32 newScalar;
            if (inAddress->mSelector == kAudioLevelControlPropertyScalarValue) {
                if (inDataSize < sizeof(Float32)) return kAudioHardwareBadPropertySizeError;
                newScalar = *((const Float32*)inData);
            } else if (inAddress->mSelector == kAudioLevelControlPropertyDecibelValue) {
                if (inDataSize < sizeof(Float32)) return kAudioHardwareBadPropertySizeError;
                newScalar = Roomcut_DBToScalar(*((const Float32*)inData));
            } else {
                return kAudioHardwareUnknownPropertyError;
            }
            if (newScalar < 0.0f) newScalar = 0.0f;
            if (newScalar > 1.0f) newScalar = 1.0f;

            pthread_mutex_lock(&gRoomcut.stateMutex);
            gRoomcut.volume = newScalar;
            pthread_mutex_unlock(&gRoomcut.stateMutex);

            if (gRoomcut_Host != NULL) {
                AudioObjectPropertyAddress changed[2] = {
                    { kAudioLevelControlPropertyScalarValue,  kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain },
                    { kAudioLevelControlPropertyDecibelValue, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain }
                };
                gRoomcut_Host->PropertiesChanged(gRoomcut_Host, kObjectID_Volume_Output_Master, 2, changed);
            }
            return kAudioHardwareNoError;
        }

        case kObjectID_Mute_Output_Master:
            if (inAddress->mSelector == kAudioBooleanControlPropertyValue) {
                if (inDataSize < sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
                bool newMuted = (*((const UInt32*)inData) != 0);

                pthread_mutex_lock(&gRoomcut.stateMutex);
                gRoomcut.muted = newMuted;
                pthread_mutex_unlock(&gRoomcut.stateMutex);

                if (gRoomcut_Host != NULL) {
                    AudioObjectPropertyAddress changed = { kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain };
                    gRoomcut_Host->PropertiesChanged(gRoomcut_Host, kObjectID_Mute_Output_Master, 1, &changed);
                }
                return kAudioHardwareNoError;
            }
            return kAudioHardwareUnknownPropertyError;

        default:
            return kAudioHardwareBadObjectError;
    }
}
