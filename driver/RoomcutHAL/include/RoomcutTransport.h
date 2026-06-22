/*
 * RoomcutTransport.h — non-real-time engine connection and shared-ring handoff.
 *
 * The worker thread owns Mach lookup, HELLO, heartbeat, mapping, and reconnect.
 * The AudioServerPlugIn IO callback only acquires the currently published ring,
 * writes to it, and releases its reader guard.
 */
#ifndef ROOMCUT_TRANSPORT_H
#define ROOMCUT_TRANSPORT_H

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>

#include "roomcut_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    mach_port_t                 servicePort;
    mach_port_t                 memoryEntry;
    mach_vm_address_t           mappedAddress;
    mach_vm_size_t              mappedSize;
    RoomcutRingHeader*          header;
    RoomcutFormatNegotiation    granted;
    /* The REAL output device's supported nominal rates, forwarded by the engine
     * in the HELLO reply so the device can advertise exactly those. 0 = engine
     * reported none (older engine, or query failed) → fall back to the static
     * list. */
    uint32_t                    availableRateCount;
    uint32_t                    availableRates[ROOMCUT_MAX_RATES];
} RoomcutTransportConnection;

/*
 * Production worker lifecycle. Start is idempotent and the detached worker
 * lives for the lifetime of coreaudiod.
 */
void Roomcut_TransportStart(uint32_t sampleRate);
void Roomcut_TransportSetSampleRate(uint32_t sampleRate);

/*
 * Invoked on the worker thread right after each successful HELLO with the real
 * output device's forwarded rate list (count == 0 means none reported). Lets
 * the CoreAudio side re-advertise kAudioDevicePropertyAvailableNominalSampleRates
 * without RoomcutTransport.c depending on CoreAudio. NULL (the default) = no-op,
 * so the host transport test links without the driver's CoreAudio glue.
 */
typedef void (*Roomcut_RatesChangedFn)(const uint32_t* rates, uint32_t count);
void Roomcut_TransportSetRatesChangedCallback(Roomcut_RatesChangedFn fn);

/*
 * Pure rate-selection policy (host-tested). Given the device's currently-set
 * nominal `current` and the real output device's supported `rates`, returns the
 * rate the device should run at: `current` if the device still supports it (so
 * we never override a working rate), otherwise the highest the device offers, so
 * an unsupported nominal (e.g. a stale 192k after switching to 48k-only AirPods)
 * is corrected. count == 0 → keep `current`.
 */
uint32_t Roomcut_PickNominalRate(uint32_t current, const uint32_t* rates, uint32_t count);

/*
 * Real-time-side guard. Every non-null acquire must be paired with release.
 * The worker waits for all guards before unmapping a retired connection.
 */
RoomcutRingHeader* Roomcut_TransportAcquireRing(void);
void Roomcut_TransportReleaseRing(void);

/*
 * Single HELLO + mapping operation used by the worker and host tests.
 * Takes ownership of servicePort even on failure.
 */
kern_return_t Roomcut_TransportConnectToPort(
    mach_port_t servicePort,
    uint32_t sampleRate,
    uint32_t timeoutMs,
    RoomcutTransportConnection* outConnection);

void Roomcut_TransportDisconnect(RoomcutTransportConnection* connection);

#ifdef __cplusplus
}
#endif

#endif /* ROOMCUT_TRANSPORT_H */
