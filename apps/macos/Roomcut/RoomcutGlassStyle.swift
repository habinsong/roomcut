//
// RoomcutGlassStyle.swift — real Liquid Glass surfaces (macOS 26).
//
// Glass is a layer treatment, not decoration: it goes only on the floating
// control/navigation layers (Now Playing, side handles/rails, the sound-controls
// sheet, the Basic/Advanced tab bar, the On toggle) — never the window
// background, buttons, sliders, or the EQ graph. We use the system glassEffect
// API so the material actually refracts the ambient background beneath it;
// Reduce Transparency falls back to an opaque token fill.
//
import SwiftUI

// MARK: - Design tokens (plan §13 Light / §14 Dark)

enum RoomcutTokens {
    // Base canvas behind everything.
    static func base(_ scheme: ColorScheme) -> Color {
        scheme == .dark ? Color(hex: 0x0F1012) : Color(hex: 0xF5F5F2)
    }
    static func baseSecondary(_ scheme: ColorScheme) -> Color {
        scheme == .dark ? Color(hex: 0x15171A) : Color(hex: 0xFBFBF9)
    }

    // Text.
    static func textPrimary(_ s: ColorScheme) -> Color { s == .dark ? Color(hex: 0xF5F5F7) : Color(hex: 0x1D1D1F) }
    static func textSecondary(_ s: ColorScheme) -> Color { s == .dark ? Color(hex: 0xA1A1A6) : Color(hex: 0x6E6E73) }
    static func textTertiary(_ s: ColorScheme) -> Color { s == .dark ? Color(hex: 0x6E6E73) : Color(hex: 0x8E8E93) }

    // Accents (precision blue + status). Same hue family across modes.
    static func blue(_ s: ColorScheme) -> Color { s == .dark ? Color(hex: 0x2997FF) : Color(hex: 0x0A84FF) }
    static let green = Color(hex: 0x34C759)
    static let amber = Color(hex: 0xFFB340)
    static let red = Color(hex: 0xFF453A)

    // Opaque fallback fill for Reduce Transparency (no blur).
    static func opaqueGlass(_ s: ColorScheme) -> Color {
        s == .dark ? Color(hex: 0x1C1E22) : Color(hex: 0xFFFFFF)
    }
}

// MARK: - Tab screens (Material content cards over the Now Playing wash)
//
// Per the Liquid Glass HIG, SECTION CONTENT is a Material (not glass — never stack
// glass on glass); glass is reserved for functional controls. So the Space/Inspect/
// Settings screens are a transparent scroll over the wash with `.regularMaterial`
// rounded cards, and any action stays a glass/native control.

struct RoomcutCard<Content: View>: View {
    @Environment(\.colorScheme) private var scheme
    @ViewBuilder var content: Content
    var body: some View {
        VStack(spacing: 0) { content }
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 18, style: .continuous)
                    .strokeBorder(.white.opacity(scheme == .dark ? 0.07 : 0.5), lineWidth: 0.5))
            .shadow(color: .black.opacity(scheme == .dark ? 0.28 : 0.06), radius: 10, y: 4)
    }
}

struct RoomcutSection<Content: View>: View {
    let title: String
    var footer: String? = nil
    let content: Content
    init(_ title: String, footer: String? = nil, @ViewBuilder content: () -> Content) {
        self.title = title
        self.footer = footer
        self.content = content()
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 7) {
            if !title.isEmpty {
                Text(title.uppercased())
                    .font(.system(size: 11, weight: .semibold)).tracking(1.1)
                    .foregroundStyle(.secondary).padding(.leading, 6)
            }
            RoomcutCard { content }
            if let footer {
                Text(footer).font(.system(size: 11)).foregroundStyle(.tertiary)
                    .padding(.leading, 6).padding(.top, 1)
            }
        }
    }
}

// Per-screen row density: a screen can tighten its rows (e.g. Settings, so the
// whole list fits above the tab bar) without affecting the others.
private struct RoomcutRowVPaddingKey: EnvironmentKey { static let defaultValue: CGFloat = 11 }
extension EnvironmentValues {
    var roomcutRowVPadding: CGFloat {
        get { self[RoomcutRowVPaddingKey.self] }
        set { self[RoomcutRowVPaddingKey.self] = newValue }
    }
}

// A row inside a card; rows are separated by `RoomcutDivider`.
struct RoomcutRow<Trailing: View>: View {
    let title: String
    var systemImage: String? = nil
    var tint: Color? = nil
    let trailing: Trailing
    @Environment(\.colorScheme) private var scheme
    @Environment(\.roomcutRowVPadding) private var vPadding
    init(_ title: String, systemImage: String? = nil, tint: Color? = nil,
         @ViewBuilder trailing: () -> Trailing) {
        self.title = title
        self.systemImage = systemImage
        self.tint = tint
        self.trailing = trailing()
    }
    var body: some View {
        HStack(spacing: 10) {
            if let systemImage {
                Image(systemName: systemImage).font(.system(size: 13, weight: .medium)).frame(width: 22)
                    .foregroundStyle(tint ?? RoomcutTokens.textSecondary(scheme))
            }
            Text(title).font(.system(size: 13)).foregroundStyle(RoomcutTokens.textPrimary(scheme))
            Spacer(minLength: 8)
            trailing
        }
        .padding(.horizontal, 16).padding(.vertical, vPadding)
    }
}

struct RoomcutDivider: View {
    var body: some View { Divider().opacity(0.4).padding(.leading, 16) }
}

struct RoomcutTabScreen<Content: View>: View {
    // `bottomPadding` keeps the last item clear of the floating tab bar (~70pt);
    // `spacing` is the gap between sections. Both default to the standard values so
    // a screen can opt into a tighter layout (Settings) without touching the rest.
    var spacing: CGFloat = 16
    var bottomPadding: CGFloat = 96
    @ViewBuilder var content: Content
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: spacing) { content }
                .padding(.horizontal, 16).padding(.top, 6).padding(.bottom, bottomPadding)
        }
        .scrollIndicators(.never)
    }
}

// MARK: - Glass surfaces

enum RoomcutGlass {
    enum Surface {
        case card        // Now Playing
        case rail        // sidebar / inspector expanded
        case handle      // collapsed edge handles
        case sheet       // bottom sound controls
        case control     // top status / toggle chrome
        case tabBar      // Basic / Advanced pill

        var cornerRadius: CGFloat {
            switch self {
            case .card:    return 40   // plan §5-4: 36–44, "glass orb" feel
            case .rail:    return 32   // §6-2: 30–36
            case .handle:  return 26   // §6-1: 24–30
            case .sheet:   return 28
            case .control: return 22
            case .tabBar:  return 999  // §9-2: pill
            }
        }

        // Subtle tint keeps surfaces from reading as plain frosted gray; kept
        // very low so it never looks like a colored button.
        func tint(_ scheme: ColorScheme) -> Color? {
            switch self {
            case .card:
                return (scheme == .dark ? Color.white : Color.white).opacity(0.04)
            default:
                return nil
            }
        }
    }
}

private struct GlassSurfaceModifier: ViewModifier {
    let surface: RoomcutGlass.Surface
    let interactive: Bool
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.colorScheme) private var scheme

    func body(content: Content) -> some View {
        // The bottom sheet rounds only its top corners (it rises from the window
        // edge); everything else is a uniform continuous-rounded rectangle.
        if surface == .sheet {
            // Bottom corners are rounder than the top so their curve matches the
            // tab-bar pill they now sit flush with (the pill's radius ≈ height/2 ≈ 30;
            // a `.continuous` corner reads flatter, so it needs a larger radius to
            // look the same).
            applied(content, shape: UnevenRoundedRectangle(
                topLeadingRadius: surface.cornerRadius,
                bottomLeadingRadius: 34,
                bottomTrailingRadius: 34,
                topTrailingRadius: surface.cornerRadius,
                style: .continuous))
        } else {
            applied(content, shape: RoundedRectangle(
                cornerRadius: surface.cornerRadius, style: .continuous))
        }
    }

    @ViewBuilder
    private func applied<S: InsettableShape>(_ content: Content, shape: S) -> some View {
        if reduceTransparency {
            // Opaque token fill + stronger edge for definition (no translucency).
            content
                .background(shape.fill(RoomcutTokens.opaqueGlass(scheme)))
                .overlay(shape.strokeBorder(RoomcutTokens.textPrimary(scheme).opacity(0.16), lineWidth: 1))
                .clipShape(shape)
                .shadow(color: .black.opacity(scheme == .dark ? 0.4 : 0.1),
                        radius: surface == .handle ? 5 : 16, y: surface == .handle ? 2 : 6)
        } else {
            content
                // A plain colour fill (NOT a glass tint, which dims when the window
                // is inactive) → the sheet / tab bar read the SAME whether or not
                // the app is focused. Sits in front of the (clear) glass, so the
                // wash still shows through at the fill's opacity.
                .background { if let fill = surfaceFill { shape.fill(fill) } }
                .glassEffect(glassStyle, in: shape)
        }
    }

    private var surfaceFill: Color? {
        switch surface {
        case .sheet, .tabBar:
            return (scheme == .dark ? Color.black : Color.white).opacity(scheme == .dark ? 0.30 : 0.36)
        default:
            return nil
        }
    }

    private var glassStyle: Glass {
        // `.clear` is less frosted / less dimmed than `.regular` (Apple exposes no
        // blur-radius knob). The sheet + tab bar sit over the media-rich Cover/Mesh
        // wash, so the clear variant is the right one and lets the background read
        // through with less blur.
        var g: Glass = (surface == .sheet || surface == .tabBar) ? .clear : .regular
        if let tint = surface.tint(scheme) { g = g.tint(tint) }
        if interactive { g = g.interactive() }
        return g
    }
}

extension View {
    /// Apply a real Liquid Glass surface (or an opaque fallback under Reduce
    /// Transparency). `interactive` adds the touch-responsive glass behaviour
    /// used on the handles / toggle chrome.
    func roomcutGlass(_ surface: RoomcutGlass.Surface, interactive: Bool = false) -> some View {
        modifier(GlassSurfaceModifier(surface: surface, interactive: interactive))
    }
}

// MARK: - Fade-edge mask (plan §12-4: no hard separators on rails)

extension View {
    func fadeEdge(_ edge: HorizontalEdge) -> some View {
        let isLeading = edge == .leading
        return self.mask(
            LinearGradient(
                stops: [
                    .init(color: .black, location: 0.0),
                    .init(color: .black, location: 0.86),
                    .init(color: .black.opacity(0.55), location: 0.96),
                    .init(color: .clear, location: 1.0),
                ],
                startPoint: isLeading ? .leading : .trailing,
                endPoint: isLeading ? .trailing : .leading
            )
        )
    }
}

// MARK: - Hex color helper

extension Color {
    init(hex: UInt32) {
        self.init(
            .sRGB,
            red: Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >> 8) & 0xFF) / 255,
            blue: Double(hex & 0xFF) / 255,
            opacity: 1
        )
    }
}
