import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The bottom tab bar, and the button style the chrome shares.
// MARK: - Bottom tab bar (Liquid Glass bar; subtle selection pill)

struct BottomTabBar: View {
    @Binding var selection: RoomcutTab
    // When the Home Cover/Mesh wash is dark (light mode), the bar + its text go white
    // to match the Now Playing chrome (item 4) — linked to NowPlayingInk. A BRIGHT
    // dark-mode wash flips them to black instead (same judgement, other direction).
    var darkBackdrop: Bool = false
    var brightBackdrop: Bool = false
    var blendsWithOpenSheet: Bool = false
    var onReselect: ((RoomcutTab) -> Void)? = nil
    @Environment(\.colorScheme) private var scheme
    @Namespace private var pillNS

    // Brighter fill + white ink over a dark wash; otherwise the normal tokens.
    // Dark mode uses plain white for the unselected tabs (the tertiary grey token
    // read as washed-out) — selection still stands out via the pill + fill glyph.
    private var unselectedInk: Color {
        if darkBackdrop { return Color.white.opacity(0.66) }
        if brightBackdrop { return Color.black.opacity(0.66) }
        return scheme == .dark ? .white : RoomcutTokens.textTertiary(scheme)
    }
    private var fillColor: Color {
        if darkBackdrop { return Color.white.opacity(0.30) }
        if brightBackdrop { return Color.white.opacity(0.28) }
        return (scheme == .dark ? Color.black : Color.white).opacity(scheme == .dark ? 0.24 : 0.28)
    }
    // Selection is monochrome (HIG: no accent on a glass tab bar over rich content) —
    // a bright/dark glass-highlight capsule reads as Liquid Glass, not a blue button.
    private var selectedInk: Color {
        if darkBackdrop { return .white }
        if brightBackdrop { return .black }
        return RoomcutTokens.textPrimary(scheme)
    }
    private var pillFill: Color {
        if darkBackdrop { return Color.white.opacity(0.24) }
        if brightBackdrop { return Color.black.opacity(0.08) }
        return (scheme == .dark ? Color.white : Color.black).opacity(scheme == .dark ? 0.16 : 0.08)
    }

    var body: some View {
        HStack(spacing: 2) {
            ForEach(RoomcutTab.allCases) { t in
                tabButton(t)
            }
        }
        .padding(.horizontal, 6)
        .padding(.vertical, 6)
        // A plain white/black fill (NOT a glass tint, which dims when the window is
        // inactive) so the bar looks the SAME focused or not; clear glass on top.
        .background(Capsule().fill(blendsWithOpenSheet ? Color.clear : fillColor))
        .glassEffect(blendsWithOpenSheet ? .identity : .clear, in: Capsule())
    }

    private func tabButton(_ t: RoomcutTab) -> some View {
        let selected = selection == t
        return Button {
            if selection == t {
                onReselect?(t)
            } else {
                withAnimation(.smooth(duration: 0.3)) { selection = t }
            }
        } label: {
            VStack(spacing: 3) {
                Image(systemName: t.icon)
                    .font(.system(size: 16, weight: .medium))
                    .symbolVariant(selected ? .fill : .none)
                Text(t.title)
                    .font(.system(size: 9, weight: selected ? .semibold : .medium))
                    .lineLimit(1).minimumScaleFactor(0.75)
            }
            .foregroundStyle(selected ? selectedInk : unselectedInk)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 7)
            .background {
                // A flat, low-opacity selection pill UNDER the glyph (no glass
                // blur on top), so the icon/label stay crisp. It slides between
                // tabs via matchedGeometryEffect.
                if selected {
                    Capsule()
                        .fill(pillFill)
                        .matchedGeometryEffect(id: "selPill", in: pillNS)
                }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(t.title)
        .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
    }
}
