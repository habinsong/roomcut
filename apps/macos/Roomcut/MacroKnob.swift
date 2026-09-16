import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The circular macro knob — drag the arc to change it, click the reading to type
// an exact value.
// MARK: - Circular arc knob (drag to change, click dB to type)

struct MacroKnob: View {
    let title: String
    let icon: String
    let color: Color
    let value: Double          // [-1, 1]
    let db: Double
    let beginEdit: () -> Void
    let onChange: (Double) -> Void
    let onSetDb: (Double) -> Void
    let onEditingEnded: () -> Void

    @Environment(\.colorScheme) private var scheme
    @State private var dragStartValue: Double?
    @State private var editing = false
    @State private var draft = ""
    @FocusState private var fieldFocused: Bool
    @State private var clickMonitor: Any?

    private let outer: CGFloat = 50   // smaller so up to 5 knobs fit one row
    private let gap: CGFloat = 0.75   // 270° gauge, bottom gap
    // Indicator position along the arc for value ∈ [-1,1]: -1 = 7 o'clock (trim 0),
    // 0 = 12 o'clock (centre), +1 = 5 o'clock (trim gap). The accent fills from the
    // centre to the indicator (a ± "pan"-style gauge that rotates with the value).
    private var posT: CGFloat { CGFloat((value + 1) / 2) * gap }
    private var centerT: CGFloat { gap / 2 }

    var body: some View {
        let lo = min(centerT, posT)
        let hi = max(centerT, posT)
        return VStack(spacing: 6) {
            ZStack {
                Circle()
                    .trim(from: 0, to: gap)
                    .stroke(color.opacity(0.38),
                            style: StrokeStyle(lineWidth: 3, lineCap: .round))
                    .rotationEffect(.degrees(135))
                Circle()
                    .trim(from: lo, to: max(hi, lo + 0.004))   // ≥ a dot at the centre for 0
                    .stroke(color, style: StrokeStyle(lineWidth: 3, lineCap: .round))
                    .rotationEffect(.degrees(135))
                Circle()
                    .fill(scheme == .dark ? Color.white.opacity(0.06) : Color.white.opacity(0.9))
                    .overlay(Circle().strokeBorder(.white.opacity(scheme == .dark ? 0.12 : 0.6), lineWidth: 0.5))
                    .shadow(color: .black.opacity(scheme == .dark ? 0.35 : 0.1), radius: 4, y: 2)
                    .frame(width: outer - 20, height: outer - 20)
                Image(systemName: icon)
                    .font(.system(size: 13, weight: .medium))
                    .foregroundStyle(color)
            }
            .frame(width: outer, height: outer)
            .contentShape(Circle())
            .gesture(dragGesture)

            Text(title)
                .font(.system(size: 11, weight: .medium))
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                .lineLimit(1)
                .minimumScaleFactor(0.8)

            captionView
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(title) " + L("매크로", "macro", "マクロ", "macro", "Makro"))
        .accessibilityValue(dbLabel(db))
        .accessibilityAdjustableAction { dir in
            switch dir {
            case .increment: onChange(min(1, value + 0.1)); onEditingEnded()
            case .decrement: onChange(max(-1, value - 0.1)); onEditingEnded()
            default: break
            }
        }
    }

    @ViewBuilder private var captionView: some View {
        if editing {
            TextField("dB", text: $draft)
                .textFieldStyle(.roundedBorder)
                .textContentType(nil)
                .autocorrectionDisabled()
                .frame(width: 54)
                .font(.system(size: 11, weight: .semibold).monospacedDigit())
                .multilineTextAlignment(.center)
                .focused($fieldFocused)
                .onSubmit { commitDraft() }
                .onExitCommand { commitDraft() }   // Esc commits/closes
        } else {
            Button {
                startEditing()
            } label: {
                Text(dbLabel(db))
                    .font(.system(size: 11, weight: .medium).monospacedDigit())
                    .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help(L("클릭해서 값을 직접 입력", "Click to enter a value", "クリックして値を入力",
                    "Cliquer pour saisir une valeur", "Zum Eingeben klicken"))
        }
    }

    private func startEditing() {
        draft = String(format: "%.1f", db)
        editing = true
        fieldFocused = true
        // Clicking anywhere else commits and closes the field (FocusState alone
        // is unreliable on macOS for this).
        clickMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { event in
            DispatchQueue.main.async { commitDraft() }
            return event
        }
    }

    // 0 dB shows no sign (it's neither + nor −); non-zero keeps +/−.
    private func dbLabel(_ v: Double) -> String {
        abs(v) < 0.05 ? "0.0 dB" : String(format: "%+.1f dB", v)
    }

    private func commitDraft() {
        guard editing else { return }
        editing = false
        fieldFocused = false
        if let m = clickMonitor { NSEvent.removeMonitor(m); clickMonitor = nil }
        let cleaned = draft.replacingOccurrences(of: "dB", with: "").trimmingCharacters(in: .whitespaces)
        guard let v = Double(cleaned) else { return }
        onSetDb(max(-6, min(6, v)))
    }

    // Rotary: the value follows the ANGLE of the touch around the knob centre
    // (12 o'clock = 0; the bottom 90° is the dead gap). Dragging clockwise turns it
    // up, counter-clockwise down — and the fill always matches where you point.
    private func angleValue(at p: CGPoint) -> Double {
        let dx = Double(p.x - outer / 2)
        let dy = Double(p.y - outer / 2)
        guard dx * dx + dy * dy > 64 else { return value }   // ignore near-centre jitter
        let deg = atan2(dx, -dy) * 180 / .pi                 // from 12 o'clock, clockwise +
        return max(-1, min(1, max(-135, min(135, deg)) / 135))
    }

    private var dragGesture: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { g in
                if dragStartValue == nil { dragStartValue = value; beginEdit() }
                onChange(angleValue(at: g.location))
            }
            .onEnded { _ in
                dragStartValue = nil
                onEditingEnded()
            }
    }
}
