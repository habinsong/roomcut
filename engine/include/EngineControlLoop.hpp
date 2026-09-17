/*
 * EngineControlLoop.hpp — the engine's control thread: owns the driver's ring
 * region and the real output device, keeps the output routed while devices come
 * and go, and answers every control message. main() resumes the sound state and
 * acquires the service port, then hands both here.
 *
 * One handler per message instead of one switch inside main() that shared a
 * dozen lambdas and locals with the device and watchdog code around it. The
 * order of every step is the one main() had; nothing here decides anything new.
 *
 * Included only by main.cpp, after EngineRuntime.hpp (whose definitions it uses).
 */
#ifndef ROOMCUT_ENGINE_CONTROL_LOOP_HPP
#define ROOMCUT_ENGINE_CONTROL_LOOP_HPP

#include "EngineRuntime.hpp"

namespace {

class EngineControlLoop {
public:
    EngineControlLoop(EngineContext& context, EngineSoundState& soundState, EngineStateStore& store,
                      PersistentState& persistent, const char* dumpTo, mach_port_t servicePort)
        : ctx(context), sound(soundState), stateStore(store), pstate(persistent), dumpPath(dumpTo),
          service(servicePort), savedRealUID(persistent.realOutputUID),
          preferredOutputUID(persistent.preferredOutputUID), keepRoomcutDefault(persistent.keepRoomcutDefault) {
        trackRealDefault();

        // Publish immutable snapshots; the render side owns its current slot.
        ctx.params.publish(sound.settings());

        watcher.install();
        ctx.analysis.start();

        // Startup recovery (docs/05-recovery): if we come up with Roomcut as the
        // system default (typical after a crash — launchd respawned us under the
        // user's last selection) and the driver does not hand us audio within the
        // window, point the default back at a real device so sound returns. A
        // HELLO disarms this: when the driver is alive, Roomcut-as-default is
        // exactly the healthy production state.
        roomcutDev = findRoomcutDevice(); // for system-volume mirroring
        ctx.volume.setSource(roomcutDev);
        restoreDeadline = std::chrono::steady_clock::now();
        if (isRoomcutDeviceUID(deviceUID(defaultOutputDevice()))) {
            restoreArmed = true;
            restoreDeadline += std::chrono::seconds(3);
            std::fprintf(stderr,
                "[engine] startup: default output is Roomcut and no driver yet; "
                "restoring a real device in 3 s unless the driver connects\n");
        }
    }

    void run() {
        while (ctx.running.load(std::memory_order_relaxed)) {
            RxBuffer rx;
            std::memset(&rx, 0, sizeof(rx));

            // Receive with a timeout so the signal-driven `running` flag is polled.
            const kern_return_t kr = mach_msg(&rx.hello.request.header,
                                              MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                              0, sizeof(rx), service,
                                              500 /* ms */, MACH_PORT_NULL);
            betweenMessages();

            if (kr == MACH_RCV_TIMED_OUT) {
                continue;
            }
            if (kr != KERN_SUCCESS) {
                std::fprintf(stderr, "[engine] recv error: %d\n", kr);
                continue;
            }
            dispatch(rx);
        }
    }

    void shutdown() {
        std::fprintf(stderr, "[engine] shutting down\n");
        ctx.running.store(false, std::memory_order_relaxed);
        ctx.analysis.stop();
        ctx.volume.stop();
        watcher.remove();
        // Stop the render callback BEFORE tearing down the ring it consumes.
        output.stop();
        output.close();
        ctx.ring.clear();

        if (dumpPath != nullptr) {
            const uint32_t got = ctx.dumpFrames.load(std::memory_order_relaxed);
            if (got > 0 && writeWavF32(dumpPath, ctx.dumpBuf.data(), got,
                                       ROOMCUT_MVP_CHANNELS, dumpSR)) {
                std::fprintf(stderr, "[engine] dump: wrote %u frames @ %u Hz to %s\n",
                             got, dumpSR, dumpPath);
            } else {
                std::fprintf(stderr, "[engine] dump: nothing captured (%s)\n", dumpPath);
            }
        }

        // Clean exit (user quit / uninstall via launchctl bootout → SIGTERM): we
        // are about to stop existing, so never leave the system pointed at the
        // soon-to-be-silent Roomcut device.
        restoreDefaultIfRoomcut(savedRealUID);
    }

private:
    // ---- routing ----

    // Resolve the render target: a user-pinned device (if present and not our own
    // virtual device) wins; otherwise the automatic policy. Keeps the manual
    // override in one place for both the HELLO open and the recovery reopen.
    AudioDeviceID pickOutput() {
        auto devs = listOutputDevices();
        if (!preferredOutputUID.empty()) {
            for (const auto& d : devs) {
                if (d.uid == preferredOutputUID && !isRoomcutDeviceUID(d.uid)) {
                    return d.id;
                }
            }
            // Pinned device is gone — fall through to policy until it returns.
        }
        return pickRenderDevice(devs, defaultOutputDevice(), savedRealUID);
    }

    // Persist the real device write-through, so a crashed engine's successor
    // knows where to point the default.
    void setSavedReal(const std::string& uid) {
        if (uid.empty() || isRoomcutDeviceUID(uid) || uid == savedRealUID) return;
        savedRealUID = uid;
        stateStore.save(pstate);
    }

    // The render-target policy can only route back to "the device the user was
    // using before Roomcut" if someone recorded it. Record it here: whenever
    // the system default IS a real device, persist it — so the moment the user
    // flips the default to Roomcut, the state file already names the right
    // target. Without this, a fresh engine whose default is already Roomcut
    // had to guess and fell back to AirPods → built-in speakers (the
    // 2026-06-12 "quiet, from the right" bug: rendering to the Mac mini's
    // internal speaker instead of the user's DAC).
    void trackRealDefault() {
        AudioDeviceID def = defaultOutputDevice();
        if (def == kAudioObjectUnknown) return;
        std::string uid = deviceUID(def);
        if (!uid.empty() && !isRoomcutDeviceUID(uid)) {
            setSavedReal(uid);
            // "Keep Roomcut as output": macOS auto-switched the default to a
            // real device (e.g. AirPods connected). Reclaim Roomcut as default
            // and route through the device that just took over — but only when
            // Roomcut is genuinely a live path (driver handed off a region),
            // never at startup. setSavedReal above already recorded the target;
            // recoverOutput (run right after this in the dirty handler) reopens
            // onto it. Setting the default back to Roomcut yields default ==
            // Roomcut next pass, so this never loops.
            if (keepRoomcutDefault && region.valid()) {
                AudioDeviceID rcDev = findRoomcutDevice();
                if (rcDev != kAudioObjectUnknown) {
                    setDefaultOutputDevice(rcDev);
                    std::fprintf(stderr,
                        "[engine] keep-default: reclaimed Roomcut (default had moved to '%s')\n",
                        uid.c_str());
                }
            }
        }
    }

    void persistSound() {
        ctx.params.publish(sound.settings());
        capturePersistentSound(pstate, sound.parameters(), sound.presetID());
        if (!stateStore.save(pstate))
            std::fprintf(stderr, "[engine] sound state save failed; current session remains active\n");
    }

    void recoverOutput(bool force = false) {
        const AudioDeviceID pick = pickOutput();
        const bool sameDevice = pick == output.deviceID();
        router.reopen({pick, output.deviceID(), output.running(), output.sampleRate(),
                       sameDevice && output.running() ? output.currentHardwareRate() : 0.0},
                      ringSR, region.valid(), std::chrono::steady_clock::now(), force, routing);
    }

    // ---- between messages: devices and watchdogs ----

    void betweenMessages() {
        // Device-world changes are handled here, between messages, so all
        // output/DSP mutation stays on this one control thread.
        const bool devicesChanged = watcher.takeChanges();
        if (devicesChanged) trackRealDefault();
        if (devicesChanged || router.retryDue(std::chrono::steady_clock::now())) recoverOutput();
        if (devicesChanged) {
            AudioDeviceID rc = findRoomcutDevice(); // id may change on driver reload
            if (rc != roomcutDev) {
                ctx.volume.setSource(rc);
                roomcutDev = rc;
            }
            ctx.volume.refresh(); // output may have changed → re-apply
        }

        // Retry missing listeners and recover missed volume notifications.
        watcher.install();
        if (output.running()) watcher.watchSR(output.deviceID());
        ctx.volume.poll();

        // Startup-recovery deadline: driver never connected → free the user's
        // audio from the dead route.
        if (restoreArmed && std::chrono::steady_clock::now() >= restoreDeadline) {
            restoreArmed = false;
            restoreDefaultIfRoomcut(savedRealUID);
        }

        // Manual bypass toggle (SIGUSR1) → render thread applies it.
        if (int t = g_bypassToggles; t != lastBypassToggles) {
            lastBypassToggles = t;
            bool nb = !ctx.bypassRequested.load(std::memory_order_relaxed);
            ctx.bypassRequested.store(nb, std::memory_order_relaxed);
            std::fprintf(stderr, "[engine] manual bypass %s\n", nb ? "ON" : "OFF");
        }
        // Safe-bypass latch is set on the render thread; log it once here.
        if (!safeBypassLogged && ctx.safeBypass.load(std::memory_order_relaxed)) {
            safeBypassLogged = true;
            std::fprintf(stderr, "[engine] SAFE BYPASS latched: DSP produced "
                                 "non-finite output; passthrough until restart\n");
        }

        // Driver-stall watchdog (docs/05: "the engine monitors the driver
        // connection"): the driver's transport worker heartbeats ~1/s even
        // while IO is idle, so a frozen writeIndex alone just means silence
        // (nothing playing / default routed elsewhere). Declare the driver
        // lost only when the feed is frozen AND the heartbeat went quiet;
        // announce the return when the feed advances again.
        if (RoomcutRingHeader* h = ctx.ring.current()) {
            const uint64_t w = __atomic_load_n(&h->writeIndex, __ATOMIC_ACQUIRE);
            switch (driverFeed.observe(w, std::chrono::steady_clock::now())) {
                case DriverFeedWatchdog::Event::Returned:
                    if (output.running()) {
                        ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::DriverReturned),
                                            std::memory_order_relaxed);
                        std::fprintf(stderr, "[engine] driver feed resumed\n");
                    } else {
                        // Feed is back but the output is still down: stay in
                        // RECOVERING; recoverOutput's OutputReopened promotes
                        // to STREAMING once the device is actually live.
                        std::fprintf(stderr,
                            "[engine] driver feed resumed; waiting for output reopen\n");
                    }
                    break;
                case DriverFeedWatchdog::Event::Lost:
                    ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::DriverLost),
                                        std::memory_order_relaxed);
                    std::fprintf(stderr, "[engine] driver feed stalled and heartbeat lost; "
                                         "rendering silence (wire: RECOVER)\n");
                    break;
                case DriverFeedWatchdog::Event::None:
                    break;
            }
        }

        // Render-runaway watchdog. The output unit pulls us at its client stream
        // format's rate, so framesRendered must advance at exactly
        // output.sampleRate() frames/s — whatever is playing, silence included.
        // A boot-time device re-rate can leave the render path pulling several
        // times real time (2026-08-01: iFi opened at 48 kHz, reopened at 384 kHz
        // moments later, then ran at 3.37x with the ring draining and underruns
        // climbing ~128k/s — the dropout warble). Nothing else caught it:
        // recoverOutput compares device+rate, both of which look unchanged, so
        // the engine stayed broken until the user restarted it by hand. Rebuild
        // the unit instead — a fresh AU comes back at 1.00x.
        if (output.running() && ringSR != 0) {
            const auto repair = router.observeRender(ctx.framesRendered.load(std::memory_order_relaxed),
                output.sampleRate(), std::chrono::steady_clock::now());
            if (repair.requested) {
                std::fprintf(stderr,
                    "[engine] render runaway: %.0f frames/s vs device %.0f Hz; rebuilding output (attempt %u/3)\n",
                    repair.framesPerSecond, output.sampleRate(), repair.attempt);
                recoverOutput(true);
            }
        }
    }

    // ---- messages ----

    void dispatch(RxBuffer& rx) {
        const auto messageID = rx.hello.request.header.msgh_id;
        if (messageID != ROOMCUT_MSG_HELLO && messageID != ROOMCUT_MSG_HEALTH_CHECK
            && !normalizeControlRequest(rx.control)) {
            std::fprintf(stderr, "[engine] malformed control message id=%d size=%u\n",
                         messageID, rx.control.raw.header.msgh_size);
            mach_msg_destroy(&rx.control.raw.header);
            return;
        }
        switch (messageID) {
            case ROOMCUT_MSG_HELLO:            onHello(rx); break;
            case ROOMCUT_MSG_HEALTH_CHECK:     onHealthCheck(rx); break;
            case ROOMCUT_MSG_SET_PRESET:
            case ROOMCUT_MSG_SET_PARAMS:
            case ROOMCUT_MSG_SET_COMPARISON:   onSoundCommand(rx, messageID); break;
            case ROOMCUT_MSG_SET_OUTPUT_DEV:   onSetOutputDevice(rx); break;
            case ROOMCUT_MSG_SET_BYPASS:       onSetBypass(rx); break;
            case ROOMCUT_MSG_SET_KEEP_DEFAULT: onSetKeepDefault(rx); break;
            case ROOMCUT_MSG_SET_HEAD_POSE:    onSetHeadPose(rx); break;
            case ROOMCUT_MSG_PROBE_CHANNEL:    onProbeChannel(rx); break;
            case ROOMCUT_MSG_SET_VOLUME_BOOST: onSetVolumeBoost(rx); break;
            case ROOMCUT_MSG_GET_COMPARISON:   onGetComparison(rx); break;
            case ROOMCUT_MSG_STATE:            onState(rx); break;
            case ROOMCUT_MSG_GET_ANALYSIS:     onGetAnalysis(rx); break;
            case ROOMCUT_MSG_GET_PARAMS:       onGetParams(rx); break;
            default:
                std::fprintf(stderr, "[engine] unknown msgh_id %d\n",
                             rx.hello.request.header.msgh_id);
                // Drain any rights to avoid leaking them.
                mach_msg_destroy(&rx.hello.request.header);
                break;
        }
    }

    void onHello(RxBuffer& rx) {
        const RoomcutHelloRequest& req = rx.hello.request;
        RoomcutFormatNegotiation granted{};
        if (!engineNegotiateHello(req, granted)) {
            mach_msg_destroy(&rx.hello.request.header);
            return;
        }
        std::fprintf(stderr, "[engine] HELLO v%u req sr=%u ch=%u cap=%u\n",
                     req.protocolVersion, req.requested.sampleRate,
                     req.requested.channels, req.requested.capacityFrames);

        ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::HelloReceived),
                            std::memory_order_relaxed);
        driverFeed.heartbeat(std::chrono::steady_clock::now());
        restoreArmed = false; // driver is alive — Roomcut-as-default is healthy

        const uint32_t sr = granted.sampleRate;
        const uint32_t cap = granted.capacityFrames;

        if (region.valid()) {
            // Detach the old ring and wait for its actual borrower,
            // rather than estimating callback lifetime with a delay.
            std::fprintf(stderr, "[engine] reconnect: retiring previous ring region\n");
            ctx.ring.clear();
            region.destroy();
        }
        if (output.running() && sr != ringSR) {
            // Retire once; the common open/start path below prepares
            // the new rate and handles any failure through normal retry.
            output.stop();
            watcher.watchSR(kAudioObjectUnknown);
            output.close();
        }

        if (region.create(cap, ROOMCUT_MVP_CHANNELS, sr) != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] region.create failed\n");
            mach_msg_destroy(&rx.hello.request.header);
            return;
        }

        // Open (but don't start) the real output device BEFORE replying:
        // the producer starts writing the moment the reply lands, and
        // AudioUnit open/initialize is slow enough (~100ms+) that an
        // after-reply open let the 4096-frame ring fill and drop the
        // first chunks (observed: dropped=2560 in the first audible
        // test). Prepare the DSP chain at the DEVICE rate (what we
        // actually output); a device/ring SR mismatch is handled by the
        // band-limited resampler (ring SR -> device SR).
        ringSR = sr;
        bool outputReady = output.running();
        if (!output.running()) {
            // Phase 5: never render to Roomcut's own device — a pinned
            // device wins, else the policy target (real default → saved
            // real → builtin → any).
            AudioDeviceID pick = pickOutput();
            OSStatus oerr = (pick == kAudioObjectUnknown)
                ? (OSStatus)kAudioHardwareBadDeviceError
                : openOutputOn(ctx, output, pick, sr, sound.settings());
            if (oerr != noErr) {
                std::fprintf(stderr, "[engine] output.open failed: %d (transfer ok, no audible out)\n",
                             (int)oerr);
                watcher.markChanged();
            } else {
                if (dumpPath != nullptr && ctx.dumpCapFrames == 0) {
                    dumpSR = (uint32_t)output.sampleRate();
                    ctx.dumpBuf.assign((size_t)kDumpMaxSeconds * dumpSR
                                       * ROOMCUT_MVP_CHANNELS, 0.0f);
                    ctx.dumpCapFrames = kDumpMaxSeconds * dumpSR;
                }
                outputReady = true;
            }
        }

        // Publish the region and start rendering BEFORE replying, then
        // wait (bounded) for the first real render pull. The reply is
        // the producer's green light: if it lands before the consumer
        // is actually draining, the producer fills the ring and splices
        // its head (observed dropped=2048 even with the device
        // pre-opened — AudioOutputUnitStart's first callback lags by
        // tens of ms).
        ctx.ring.publish(region.header());
        bool startedNow = false;
        const uint64_t framesBeforeStart = ctx.framesRendered.load(std::memory_order_relaxed);
        if (outputReady && !output.running()) {
            OSStatus oerr = output.start();
            if (oerr != noErr) {
                std::fprintf(stderr, "[engine] output.start failed: %d\n", (int)oerr);
                watcher.markChanged();
            } else {
                startedNow = true;
            }
        }
        if (output.running()) {
            bool renderLive = false;
            for (int i = 0; i < 200; ++i) {                    // <= 400 ms
                if (ctx.framesRendered.load(std::memory_order_relaxed) > framesBeforeStart) {
                    renderLive = true;
                    break;
                }
                usleep(2000);
            }
            if (!renderLive) {
                std::fprintf(stderr,
                    "[engine] render callback not live after 400ms; replying anyway\n");
            }
        }

        // Tell the driver which rates the REAL output device supports so
        // it can advertise exactly those — coreaudiod then settles on a
        // rate the device runs natively and the render path is a
        // passthrough (no muffling from a mismatched ring rate).
        uint32_t devRates[ROOMCUT_MAX_RATES];
        AudioDeviceID outDev = output.running() ? output.deviceID() : pickOutput();
        uint32_t devRateCount =
            deviceAvailableSampleRates(outDev, devRates, ROOMCUT_MAX_RATES);
        {
            std::string rl;
            for (uint32_t i = 0; i < devRateCount; ++i)
                rl += (i ? "," : "") + std::to_string(devRates[i]);
            std::fprintf(stderr, "[engine] HELLO reply: forwarding %u device rate(s) [%s]\n",
                         devRateCount, rl.c_str());
        }

        const kern_return_t kr = engineReplyHello(req, region, granted, devRates, devRateCount);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] engineReplyHello failed: %d\n", kr);
            if (kr == KERN_INVALID_ARGUMENT) mach_msg_destroy(&rx.hello.request.header);
            // Retirement waits for any outstanding ring borrow.
            ctx.ring.clear();
            region.destroy();
            return;
        }

        ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::RegionCreated),
                            std::memory_order_relaxed);
        std::fprintf(stderr, "[engine] handed off region (sr=%u cap=%u)\n", sr, cap);

        if (startedNow) {
            router.adopt(output.deviceID(), std::chrono::steady_clock::now());
            watcher.watchSR(output.deviceID());
            setSavedReal(deviceUID(output.deviceID()));
            ctx.volume.setTarget(output.deviceID());
        }
        if (output.running()) {
            // Promote on EVERY successful handoff, not only on startedNow:
            // when the driver's first HELLO reply loses its timeout race
            // and it retries, the output is already open, and skipping the
            // promotions parked the lifecycle at BUFFER_MAPPED — a state
            // with no OutputLost/OutputReopened exits, so the engine
            // streamed audio forever while the wire reported STOPPED
            // (menu-bar icon off, the app's default-output claim gated on
            // RUNNING never fired). Both events are no-ops when already
            // STREAMING.
            ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputOpened),
                                std::memory_order_relaxed);
            // OUTPUT_READY → STREAMING (both region + output live).
            ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputReopened),
                                std::memory_order_relaxed);
            std::fprintf(stderr, "[engine] output device open '%s' @ %.0f Hz, %u ch; STREAMING\n",
                         deviceName(output.deviceID()).c_str(),
                         output.sampleRate(), ROOMCUT_MVP_CHANNELS);
        }
    }

    void onHealthCheck(RxBuffer& rx) {
        const RoomcutHealthRequest& hreq = rx.health.request;
        if (!roomcut_health_request_valid(&hreq)) {
            mach_msg_destroy(&rx.health.request.header);
            return;
        }
        driverFeed.heartbeat(std::chrono::steady_clock::now());
        const auto coarse = presentedEngineState(ctx.lifecycle.load(),
            ctx.bypassRequested.load(std::memory_order_relaxed), ctx.safeBypass.load(std::memory_order_relaxed));
        // Carry the real output device's rates so the driver can correct
        // its nominal rate after a live device switch (which doesn't
        // re-HELLO) → keeps the ring at the device's native rate.
        uint32_t hbRates[ROOMCUT_MAX_RATES];
        AudioDeviceID hbDev = output.running() ? output.deviceID() : pickOutput();
        uint32_t hbRateCount = deviceAvailableSampleRates(hbDev, hbRates, ROOMCUT_MAX_RATES);
        const kern_return_t kr = heartbeatRespond(hreq, static_cast<uint32_t>(coarse), hbRates, hbRateCount);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] heartbeatRespond failed: %d\n", kr);
        }
        // Piggyback a render progress line so the passthrough can be
        // observed headlessly (peak of the last rendered block, total
        // frames out, and any output-side underruns). Opt-in only
        // (ROOMCUT_LOG_RENDER) — it fires every heartbeat, so leaving it
        // on grows the daemon log without bound.
        static const bool kLogRender = std::getenv("ROOMCUT_LOG_RENDER") != nullptr;
        if (output.running() && kLogRender) {
            uint32_t bits = ctx.renderPeakBits.load(std::memory_order_relaxed);
            float peak;
            std::memcpy(&peak, &bits, sizeof(peak));
            std::fprintf(stderr,
                "[engine] render: peak=%.5f framesOut=%llu underruns=%llu %s\n",
                peak,
                (unsigned long long)ctx.framesRendered.load(std::memory_order_relaxed),
                (unsigned long long)ctx.renderUnderruns.load(std::memory_order_relaxed),
                peak > 1e-5f ? "NON-SILENCE" : "silence");
        }
    }

    void onSoundCommand(RxBuffer& rx, int messageID) {
        const auto status = applySoundCommand(rx.control, sound);
        if (status == 0) persistSound();
        else std::fprintf(stderr, "[engine] sound command %d rejected: %u\n", messageID, status);
        const kern_return_t kr = controlReplyAck(rx.control.raw.header, static_cast<uint32_t>(messageID), status);
        if (kr != KERN_SUCCESS)
            std::fprintf(stderr, "[engine] sound command ack failed: %d\n", kr);
    }

    void onSetOutputDevice(RxBuffer& rx) {
        const RoomcutSetDeviceRequest& dreq = rx.control.setDevice;
        char uid[ROOMCUT_DEVICE_UID_MAX];
        std::memcpy(uid, dreq.deviceUID, sizeof(uid));
        uid[sizeof(uid) - 1] = '\0';
        preferredOutputUID = uid; // "" = back to automatic policy
        stateStore.save(pstate);
        std::fprintf(stderr, "[engine] output device pinned -> '%s'\n",
                     preferredOutputUID.empty() ? "(auto)" : preferredOutputUID.c_str());
        // Reopen on the new target via the control loop's recoverOutput.
        watcher.markChanged();
        const kern_return_t kr = controlReplyAck(dreq.header, ROOMCUT_MSG_SET_OUTPUT_DEV, 0);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] set-device ack failed: %d\n", kr);
        }
    }

    void onSetBypass(RxBuffer& rx) {
        const RoomcutSetBypassRequest& breq = rx.control.setBypass;
        const bool nb = breq.bypass != 0;
        ctx.bypassRequested.store(nb, std::memory_order_relaxed);
        std::fprintf(stderr, "[engine] manual bypass %s (ctl)\n", nb ? "ON" : "OFF");
        const kern_return_t kr = controlReplyAck(breq.header, ROOMCUT_MSG_SET_BYPASS, 0);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] bypass ack failed: %d\n", kr);
        }
    }

    void onSetKeepDefault(RxBuffer& rx) {
        const RoomcutSetKeepDefaultRequest& kreq = rx.control.setKeepDefault;
        keepRoomcutDefault = kreq.on != 0;
        stateStore.save(pstate);
        std::fprintf(stderr, "[engine] keep-default %s (ctl)\n",
                     keepRoomcutDefault ? "ON" : "OFF");
        // Apply immediately: if it's now on and the default has already
        // drifted off Roomcut, reclaim on the next device tick.
        if (keepRoomcutDefault) watcher.markChanged();
        const kern_return_t kr = controlReplyAck(kreq.header, ROOMCUT_MSG_SET_KEEP_DEFAULT, 0);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] keep-default ack failed: %d\n", kr);
        }
    }

    void onSetHeadPose(RxBuffer& rx) {
        const RoomcutSetHeadPoseRequest& hreq = rx.control.setHeadPose;
        const float yaw = static_cast<float>(hreq.yawDeg);
        uint32_t yawBits = 0;
        std::memcpy(&yawBits, &yaw, sizeof(yawBits));
        ctx.headYawBits.store(yawBits, std::memory_order_relaxed);
        ctx.headPoseActive.store(hreq.active != 0, std::memory_order_relaxed);
        const kern_return_t kr = controlReplyAck(hreq.header, ROOMCUT_MSG_SET_HEAD_POSE, 0);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] head pose ack failed: %d\n", kr);
        }
    }

    void onProbeChannel(RxBuffer& rx) {
        const RoomcutProbeChannelRequest& preq = rx.control.probeChannel;
        const float seconds = static_cast<float>(preq.seconds);
        const float level = static_cast<float>(preq.levelDb);
        uint32_t bits = 0;
        std::memcpy(&bits, &seconds, sizeof(bits));
        ctx.probeSecondsBits.store(bits, std::memory_order_relaxed);
        std::memcpy(&bits, &level, sizeof(bits));
        ctx.probeLevelBits.store(bits, std::memory_order_relaxed);
        ctx.probeChannel.store(preq.channel, std::memory_order_relaxed);
        ctx.probeGeneration.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[engine] probe channel %d, %.2f s at %.1f dB (ctl)\n",
                     preq.channel, preq.seconds, preq.levelDb);
        const kern_return_t kr = controlReplyAck(preq.header, ROOMCUT_MSG_PROBE_CHANNEL, 0);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] probe ack failed: %d\n", kr);
        }
    }

    void onSetVolumeBoost(RxBuffer& rx) {
        const RoomcutSetVolumeBoostRequest& vreq = rx.control.setVolumeBoost;
        const double boost = clampVolumeBoost(vreq.boost);
        ctx.volume.setBoost((float)boost);
        pstate.volumeBoost = boost;
        stateStore.save(pstate);
        const kern_return_t kr = controlReplyAck(vreq.header, ROOMCUT_MSG_SET_VOLUME_BOOST, 0);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] volume boost ack failed: %d\n", kr);
        }
    }

    void onGetComparison(RxBuffer& rx) {
        ctx.comparisonMeters.readLatest(ctx.comparisonSnapshot);
        const auto reply = makeComparisonReply(sound, ctx.comparisonSnapshot, output.running(),
            ctx.bypassRequested.load(std::memory_order_relaxed));
        controlReplyComparison(rx.control.getComparison, reply);
    }

    void onState(RxBuffer& rx) {
        EngineStatusSnapshot snapshot;
        snapshot.lifecycle = ctx.lifecycle.load();
        snapshot.manualBypass = ctx.bypassRequested.load(std::memory_order_relaxed);
        snapshot.safeBypass = ctx.safeBypass.load(std::memory_order_relaxed);
        snapshot.outputRunning = output.running();
        const uint32_t grBits = ctx.limiterGRBits.load(std::memory_order_relaxed);
        const uint32_t pkBits = ctx.renderPeakBits.load(std::memory_order_relaxed);
        std::memcpy(&snapshot.limiterReductionDb, &grBits, sizeof(float));
        std::memcpy(&snapshot.renderPeak, &pkBits, sizeof(float));
        snapshot.framesRendered = ctx.framesRendered.load(std::memory_order_relaxed);
        snapshot.ringUnderruns = ctx.renderUnderruns.load(std::memory_order_relaxed);
        if (snapshot.outputRunning) snapshot.outputDeviceUID = deviceUID(output.deviceID());
        snapshot.keepDefault = keepRoomcutDefault;
        snapshot.volumeBoost = ctx.volume.boost();
        snapshot.engineLatencyMs = output.running() ? ctx.engineLatencyMs : 0.0;
        snapshot.bedRendererAttached = output.running() && ctx.bedAttached;
        if (snapshot.bedRendererAttached) {
            snapshot.bedPersonalizedHrtf = ctx.bedCurrent.personalizedHrtfInUse() || ctx.bedReference.personalizedHrtfInUse();
            snapshot.bedUnitRate = static_cast<float>(ctx.bedCurrent.unitRate());
            const uint32_t bedBits = ctx.bedExternalGainBits.load(std::memory_order_relaxed);
            std::memcpy(&snapshot.bedExternalGain, &bedBits, sizeof(float));
        }
        const kern_return_t kr = controlReplyState(rx.control.stateRequest, makeStateReply(sound, snapshot));
        if (kr != KERN_SUCCESS)
            std::fprintf(stderr, "[engine] state reply failed: %d\n", kr);
    }

    void onGetAnalysis(RxBuffer& rx) {
        const kern_return_t kr = controlReplyAnalysis(rx.control.analysisRequest, makeAnalysisReply(ctx.analysis.snapshot()));
        if (kr != KERN_SUCCESS)
            std::fprintf(stderr, "[engine] analysis reply failed: %d\n", kr);
    }

    void onGetParams(RxBuffer& rx) {
        const kern_return_t kr = controlReplyParams(rx.control.getParams, makeParamsReply(sound));
        if (kr != KERN_SUCCESS)
            std::fprintf(stderr, "[engine] params reply failed: %d\n", kr);
    }

    // ---- state ----

    EngineContext&     ctx;
    EngineSoundState&  sound;
    EngineStateStore&  stateStore;
    PersistentState&   pstate;
    const char* const  dumpPath;
    const mach_port_t  service;

    // The region + output device must outlive the render callback; owned here.
    RingRegion    region;
    OutputDevice  output;
    DeviceWatcher watcher;
    uint32_t      dumpSR = 0;  // device SR at capture time (output.close() zeroes it)
    uint32_t      ringSR = 0;  // negotiated ring SR (0 until first HELLO)
    std::string&  savedRealUID;        // survives engine death
    std::string&  preferredOutputUID;  // user-pinned device
    bool&         keepRoomcutDefault;  // reclaim default toggle

    int  lastBypassToggles = 0;
    bool safeBypassLogged  = false;
    DriverFeedWatchdog driverFeed{std::chrono::steady_clock::now()};
    AudioDeviceID roomcutDev = kAudioObjectUnknown;
    bool restoreArmed = false;
    std::chrono::steady_clock::time_point restoreDeadline;
    OutputRouter router{std::chrono::steady_clock::now()};

    // The router owns the order and the retry policy; these are the HAL calls and
    // engine-state updates it drives, all on this control thread.
    OutputRouter::Operations routing{
        .lost = [this](const char* reason) {
            std::fprintf(stderr, "[engine] device change: reopening output (%s)\n", reason);
            ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputLost),
                                std::memory_order_relaxed);
        },
        .stop = [this] { output.stop(); },
        .unwatchRate = [this] { watcher.watchSR(kAudioObjectUnknown); },
        .close = [this] { output.close(); },
        .open = [this](uint32_t device, bool matchRingRate) {
            return (int)openOutputOn(ctx, output, device, ringSR, sound.settings(), matchRingRate);
        },
        .start = [this] { return (int)output.start(); },
        .failed = [this](int error) {
            std::fprintf(stderr, "[engine] output reopen failed: %d (retry scheduled)\n", error);
            output.close();
        },
        .restored = [this](uint32_t device) {
            watcher.watchSR(device);
            setSavedReal(deviceUID(device));
            ctx.volume.setTarget(device);
            // While the driver feed is stalled the output alone doesn't make us
            // streaming; the watchdog's DriverReturned promotes when it resumes.
            if (!driverFeed.stalled()) {
                ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputReopened),
                                    std::memory_order_relaxed);
            }
            std::fprintf(stderr, "[engine] output -> '%s' @ %.0f Hz\n",
                         deviceName(device).c_str(), output.sampleRate());
        },
    };
};

} // namespace

#endif // ROOMCUT_ENGINE_CONTROL_LOOP_HPP
