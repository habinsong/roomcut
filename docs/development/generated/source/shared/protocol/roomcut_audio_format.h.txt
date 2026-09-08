/*
 * roomcut_audio_format.h
 *
 * Canonical audio format constants shared by Roomcut.driver, RoomcutAudioEngine,
 * and RoomcutCore. Keep this header dependency-free (plain C, no includes beyond
 * stdint) so the sandboxed Audio Server Plug-in can include it safely.
 */
#ifndef ROOMCUT_AUDIO_FORMAT_H
#define ROOMCUT_AUDIO_FORMAT_H

#include <stdint.h>

/* MVP format. Internal processing is always 32-bit float. */
#define ROOMCUT_MVP_CHANNELS        2
#define ROOMCUT_BYTES_PER_SAMPLE    4   /* float32 */

/* Common PCM rate constants — used as fallback/default values (e.g. 48 kHz) and
 * for the driver's pre-connection static rate list. The ACTUAL supported rates
 * are whatever the real output device reports, recognized live (see the engine's
 * deviceAvailableSampleRates and the driver's dynamic rate list) — there is no
 * fixed ceiling. */
#define ROOMCUT_SR_44100            44100
#define ROOMCUT_SR_48000            48000
#define ROOMCUT_SR_88200            88200
#define ROOMCUT_SR_96000            96000
#define ROOMCUT_SR_176400           176400
#define ROOMCUT_SR_192000           192000

/* Size of the static fallback rate list (used only before the engine forwards
 * the real device's rates). Ordered low → high. */
#define ROOMCUT_SR_COUNT 6

/* Upper bound on the per-device rate list the engine reports to the driver over
 * HELLO (covers hi-res DACs that advertise up to 768 kHz). */
#define ROOMCUT_MAX_RATES 16

/* Sanity gate only — NOT a supported-rate table. The real device's actual rates
 * are recognized live; this just rejects garbage values. */
static inline int roomcut_sr_supported(uint32_t sr) {
    return sr >= 8000u && sr <= 768000u;
}

/* Sample layout on the wire / in the ring buffer: interleaved float32 LRLR... */
typedef float roomcut_sample_t;

#endif /* ROOMCUT_AUDIO_FORMAT_H */
