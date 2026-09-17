/*
 * SpatialMixerDiffuseField.hpp — AUSpatialMixer's own diffuse-field response,
 * as measured, so the bed renderer can take it back out.
 *
 * What was measured (2026-09-16, AUSpatialMixer component version 0x00010608,
 * headphones, UseOutputType, AmbienceBed, no internal reverb): one impulse from
 * each of 494 directions (elevation -45..90 in 15-degree rings, 5-degree
 * azimuth steps shrinking with the ring's circumference), the power of both ears
 * averaged in 1/6-octave bands and weighted by the area each direction stands
 * for, relative to the impulse played dry. At 48 kHz. 44.1 and 96 kHz agreed to
 * within 0.02 dB up to 11 kHz and within 1.3 dB at 16 kHz.
 *
 * The level is -4 dB flat below 200 Hz; the shape above it (+1.5 dB at 12.8 kHz
 * against -2.5 at 2 kHz, -4.4 at 5 kHz) is what the output of the product chain
 * measured as its tone against the dry programme: 2.2 dB RMS deviation with it,
 * 1.6 dB with it divided out. test_spatial_mixer_bed measures the unit again and
 * fails if a later macOS has moved it.
 */
#ifndef ROOMCUT_SPATIAL_MIXER_DIFFUSE_FIELD_HPP
#define ROOMCUT_SPATIAL_MIXER_DIFFUSE_FIELD_HPP

#include "dsp/ParametricFit.hpp"

namespace roomcut {

inline constexpr ResponsePoint kSpatialMixerDiffuseField48k[] = {
    {    50.0, -4.040}, {    56.1, -4.038}, {    63.0, -4.035}, {    70.7, -4.031},
    {    79.4, -4.026}, {    89.1, -4.022}, {   100.0, -4.017}, {   112.2, -4.012},
    {   126.0, -4.008}, {   141.4, -4.006}, {   158.7, -4.007}, {   178.2, -4.013},
    {   200.0, -4.027}, {   224.5, -4.049}, {   252.0, -4.079}, {   282.8, -4.111},
    {   317.5, -4.133}, {   356.4, -4.124}, {   400.0, -4.058}, {   449.0, -3.924},
    {   504.0, -3.745}, {   565.7, -3.589}, {   635.0, -3.542}, {   712.7, -3.621},
    {   800.0, -3.715}, {   898.0, -3.673}, {  1007.9, -3.531}, {  1131.4, -3.366},
    {  1269.9, -3.131}, {  1425.4, -3.042}, {  1600.0, -2.934}, {  1795.9, -2.708},
    {  2015.9, -2.481}, {  2262.7, -2.630}, {  2539.8, -2.288}, {  2850.9, -2.543},
    {  3200.0, -2.562}, {  3591.9, -2.437}, {  4031.7, -2.691}, {  4525.5, -3.787},
    {  5079.7, -4.380}, {  5701.8, -3.118}, {  6400.0, -0.720}, {  7183.8, +0.732},
    {  8063.5, +0.118}, {  9051.0, -1.292}, { 10159.4, -2.474}, { 11403.5, -0.305},
    { 12800.0, +1.542}, { 14367.5, -1.824}, { 16127.0, -5.896}, { 18101.9, -10.569},
};

} // namespace roomcut

#endif // ROOMCUT_SPATIAL_MIXER_DIFFUSE_FIELD_HPP
