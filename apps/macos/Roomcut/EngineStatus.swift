import Foundation
import CRoomcutClient
import RoomcutPresentationCore

// One snapshot of what the engine is doing, as the app sees it. The capability
// bits matter more than they look: the app has to keep working against an engine
// that predates any given feature, so every feature asks here first.
public struct EngineStatus {
    // The C header's anonymous enum imports as plain Int32 constants.
    public static let stopped = UInt32(ROOMCUT_CLIENT_STATE_STOPPED)
    public static let running = UInt32(ROOMCUT_CLIENT_STATE_RUNNING)
    public static let bypass  = UInt32(ROOMCUT_CLIENT_STATE_BYPASS)
    public static let recover = UInt32(ROOMCUT_CLIENT_STATE_RECOVER)
    public static let spatialParamsCapability = UInt32(ROOMCUT_CLIENT_CAP_SPATIAL_PARAMS)
    public static let parametricCapability = UInt32(ROOMCUT_CLIENT_CAP_PARAMETRIC)
    public static let dynamicEqCapability = UInt32(ROOMCUT_CLIENT_CAP_DYNAMIC_EQ)
    public static let analyzerCapability = UInt32(ROOMCUT_CLIENT_CAP_ANALYZER)
    public static let dynamicsCapability = UInt32(ROOMCUT_CLIENT_CAP_DYNAMICS)
    public static let levelMatchCapability = UInt32(ROOMCUT_CLIENT_CAP_LEVEL_MATCH)
    public static let virtualRoomCapability = UInt32(ROOMCUT_CLIENT_CAP_VIRTUAL_ROOM)
    public static let upmixCapability = UInt32(ROOMCUT_CLIENT_CAP_UPMIX)
    public static let headTrackingCapability = UInt32(ROOMCUT_CLIENT_CAP_HEAD_TRACKING)

    public var reachable = false
    public var state: UInt32 = EngineStatus.stopped
    public var presetId = "—"
    public var manualBypass = false
    public var safeBypass = false
    public var limiterGRDb: Float = 0
    public var peak: Float = 0
    public var paramsRevision: UInt32 = 0
    public var frames: UInt64 = 0
    public var underruns: UInt64 = 0
    public var outputDeviceUID = ""
    public var keepDefault = false
    public var capabilities: UInt32 = 0
    public var volumeBoost = 1.0
    // What the engine adds on purpose. 0 means an engine that doesn't report it.
    public var engineLatencyMs = 0.0

    public init() {}

    public var supportsLevelMatch: Bool { capabilities & Self.levelMatchCapability != 0 }

    public var supportsSpatialParams: Bool {
        (capabilities & Self.spatialParamsCapability) != 0
    }

    public var supportsParametric: Bool {
        (capabilities & Self.parametricCapability) != 0
    }

    public var supportsAnalyzer: Bool {
        (capabilities & Self.analyzerCapability) != 0
    }

    public var supportsDynamics: Bool {
        (capabilities & Self.dynamicsCapability) != 0
    }

    public var supportsDynamicEq: Bool {
        (capabilities & Self.dynamicEqCapability) != 0
    }

    public var supportsVirtualRoom: Bool {
        (capabilities & Self.virtualRoomCapability) != 0
    }
    public var supportsUpmix: Bool {
        (capabilities & Self.upmixCapability) != 0
    }

    public var supportsHeadTracking: Bool {
        (capabilities & Self.headTrackingCapability) != 0
    }

    public var stateName: String {
        guard reachable else { return "OFFLINE" }
        switch state {
        case Self.stopped: return "STOPPED"
        case Self.running: return "RUNNING"
        case Self.bypass:  return "BYPASS"
        case Self.recover: return "RECOVER"
        default: return "?"
        }
    }

    public var menuBarSymbol: String {
        guard reachable else { return "waveform.slash" }
        switch state {
        case Self.running: return "waveform"
        case Self.bypass:  return "waveform.slash"
        // Recovering is transient (the engine reconnects to the driver on every
        // app launch now that it follows the app's lifecycle). Don't flash an
        // alarming ⚠️ in the menu bar for it — the in-app status still shows
        // "복구 중". Treat it as active.
        case Self.recover: return "waveform"
        default: return "waveform.slash"
        }
    }

    public var presentation: RoomcutPresentation.Status {
        RoomcutPresentation.status(reachable: reachable, state: state)
    }
}

// One parametric-EQ band, mirroring RoomcutClientParamBand. `type` indexes the
// filter kinds below (also the engine's BiquadType order).
