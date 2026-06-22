import Foundation

public enum RoomcutTheme {
    public enum SurfaceRole: String, Equatable {
        case window
        case panel
        case control
        case meter
    }

    public enum MeterRole: String, Equatable {
        case peak
        case limiter
        case warning
    }

    public struct Layout: Equatable {
        public let menuWidth: Double
        public let menuMaxHeight: Double
        public let mainWindowWidth: Double
        public let mainWindowHeight: Double
        public let mainWindowMinWidth: Double
        public let mainWindowMinHeight: Double
        public let inspectorWidth: Double
        public let spacingUnit: Double
        public let cornerRadius: Double
    }

    public static let layout = Layout(
        menuWidth: 340,
        menuMaxHeight: 420,
        mainWindowWidth: 1040,
        mainWindowHeight: 700,
        mainWindowMinWidth: 920,
        mainWindowMinHeight: 620,
        inspectorWidth: 240,
        spacingUnit: 8,
        cornerRadius: 8
    )

    public static func surfaceRole(for panelDepth: Int) -> SurfaceRole {
        panelDepth <= 0 ? .window : .panel
    }

    public static func meterRole(limiterActive: Bool, underrunsVisible: Bool) -> MeterRole {
        if underrunsVisible { return .warning }
        if limiterActive { return .limiter }
        return .peak
    }
}
