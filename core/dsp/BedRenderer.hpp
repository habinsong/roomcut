/*
 * BedRenderer.hpp — a headphone renderer for an upmixed bed that lives outside
 * the DSP core.
 *
 * core/dsp stays platform-independent; the engine can still hand the bed to a
 * platform renderer (AUSpatialMixer) through this interface. SurroundStage
 * never learns what is behind it.
 *
 * Contract the stage relies on:
 *   - blockFrames() is fixed once the renderer is attached and prepared, and
 *     every render() call produces exactly that many frames.
 *   - latencyFrames() is fixed the same way: how many frames each rendered
 *     block lags the block it was given (0 when output frame i answers input
 *     frame i). The stage delays everything else by as much.
 *   - canRender() and render() allocate nothing, take no locks and do no I/O.
 *   - Channels arrive in Upmixer order: C, L, R, Ls, Rs, Lb, Rb. A 5.1 render
 *     reads the first five.
 */
#ifndef ROOMCUT_BED_RENDERER_HPP
#define ROOMCUT_BED_RENDERER_HPP

#include <cstddef>

namespace roomcut {

class BedRenderer {
public:
    virtual ~BedRenderer() = default;

    virtual std::size_t blockFrames() const = 0;
    virtual std::size_t latencyFrames() const { return 0; }

    // Upmixer::k51 or Upmixer::k71 at the rate the renderer was prepared for.
    virtual bool canRender(int layout) const = 0;

    // Head yaw in degrees, positive when the listener turns right.
    virtual void render(int layout, const float* const* channels, double headYawDegrees,
                        float* left, float* right) = 0;
};

} // namespace roomcut

#endif // ROOMCUT_BED_RENDERER_HPP
