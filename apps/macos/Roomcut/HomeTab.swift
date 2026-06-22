//
// HomeTab.swift — Now Playing on top, a Sound Controls sheet docked to the
// bottom that the user drags up (open) / down (closed) in ONE step.
//
//   closed: the sheet shrinks into the tab bar's band — only the thin "—" handle
//           shows above it (no EQ text). Drag the handle up to open.
//   open:   Now Playing collapses to a compact pill; the sheet shows knobs, a
//           volume bar, the EQ summary, and a Basic/Advanced glass segment
//           pinned just above the tab bar. A drag snaps to the detent nearest the
//           projected (velocity-aware) release — medium only if it settles there.
//
// Reuses NowPlayingView, RoomcutMacroControls and AdvancedControls.
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// Sheet open/close state + detent math, shared between HomeTab (which draws the
// glass panel + controls) and the drag handle that lives ON TOP of the tab bar in
// RoomcutAppCanvas — so the handle can sit inside the tab bar yet still drive the
// sheet.
@MainActor
final class SoundSheetModel: ObservableObject {
    @Published var level: SoundControlsLevel
    @Published var dragOffset: CGFloat = 0
    // Latest content-area height (from HomeTab's GeometryReader) so the handle can
    // run the same snap math from RoomcutAppCanvas.
    var lastTotal: CGFloat = 600

    let tabClear: CGFloat = 80           // keep content above the floating tab bar
    let minimizedSheetHeight: CGFloat = 57
    private let sheetLift: CGFloat = 28
    // A VERY slight spring (high damping → barely any bounce) so the snap settles
    // with a soft pull. The old "shake" was the scroll view bouncing mid-drag, which
    // is now disabled while dragging — so this gentle spring is safe.
    var snap: Animation { .spring(response: 0.42, dampingFraction: 0.92) }

    init(level: SoundControlsLevel) { self.level = level }

    var isExpanded: Bool { level == .expanded }

    func restingHeight(_ l: SoundControlsLevel, total: CGFloat) -> CGFloat {
        switch l {
        case .minimized: return minimizedSheetHeight
        case .controls:  return min(total * 0.90, 120 + tabClear + sheetLift)
        case .expanded:  return min(total * 0.90, total * 0.80 + sheetLift)
        }
    }

    func sheetHeight(total: CGFloat) -> CGFloat {
        min(total * 0.90, max(minimizedSheetHeight, restingHeight(level, total: total) - dragOffset))
    }

    // Track the finger 1:1, no animation.
    func dragChanged(_ translation: CGFloat) {
        var t = Transaction(); t.disablesAnimations = true
        withTransaction(t) { dragOffset = translation }
    }

    // Snap to the detent NEAREST the velocity-projected release height — never
    // springs back against the drag direction. EXCEPTION: dragging an OPEN sheet
    // DOWN past ~30% of its travel pulls it straight closed (skip the medium detent)
    // — "pull down to close" (item 3).
    func dragEnded(predictedTranslation: CGFloat) {
        let total = lastTotal
        let base = restingHeight(level, total: total)
        let projected = min(total * 0.90, max(minimizedSheetHeight, base - predictedTranslation))
        let target: SoundControlsLevel
        let closeBelow = base - (base - minimizedSheetHeight) * 0.10
        if level != .minimized, predictedTranslation > 0, projected < closeBelow {
            target = .minimized
        } else {
            target = [.minimized, .controls, .expanded].min {
                abs(restingHeight($0, total: total) - projected) < abs(restingHeight($1, total: total) - projected)
            } ?? .minimized
        }
        // Commit the target NOW so the Now Playing layout transitions together with the
        // sheet, while keeping the height continuous so it SLIDES (no jump, no clamp
        // bounce): switch `level` and pre-load `dragOffset` with the gap to the target
        // height in one instant step (zero visual change), then spring `dragOffset` to 0.
        // Deriving the height purely from the enum makes the base jump when `level`
        // flips — the "튀어나온" pop that pushed the layout around.
        let current = sheetHeight(total: total)
        let settleBase = restingHeight(target, total: total)
        var instant = Transaction(); instant.disablesAnimations = true
        withTransaction(instant) { level = target; dragOffset = settleBase - current }
        withAnimation(snap) { dragOffset = 0 }
    }

    func toggleExpanded() {
        withAnimation(snap) { level = isExpanded ? .minimized : .expanded; dragOffset = 0 }
    }

    // Arrow-key control (gated to the Home tab via `keyboardEnabled`): ↑ opens one
    // detent, ↓ closes one.
    var keyboardEnabled = false
    func stepUp() { withAnimation(snap) { level = level.expandedOneStep(); dragOffset = 0 } }
    func stepDown() { withAnimation(snap) { level = level.collapsedOneStep(); dragOffset = 0 } }
}

struct HomeTab: View {
    @ObservedObject var model: RoomcutViewModel
    @ObservedObject var monitor: NowPlayingMonitor
    // Sheet state lives in a shared model so the tab-bar handle can drive it.
    @ObservedObject var sheet: SoundSheetModel
    @Environment(\.colorScheme) private var scheme

    enum Segment: String, CaseIterable {
        case basic = "Basic", advanced = "Advanced"
        var displayName: String {
            switch self {
            case .basic:    return L("Basic", "Basic", "基本", "Basique", "Einfach")
            case .advanced: return L("Advanced", "Advanced", "詳細", "Avancé", "Erweitert")
            }
        }
    }
    @State private var segment: Segment = .basic

    init(model: RoomcutViewModel, monitor: NowPlayingMonitor, sheet: SoundSheetModel) {
        self.model = model
        self.monitor = monitor
        self.sheet = sheet
        let advanced = AppLaunch.fixtureKind == .uiAdvanced || AppLaunch.fixtureKind == .uiAnalyzer
        _segment = State(initialValue: advanced ? .advanced : .basic)
    }

    var body: some View {
        GeometryReader { geo in
            let total = geo.size.height
            let sheetH = sheet.sheetHeight(total: total)
            // Drive the morph off the LIVE sheet height (not the discrete level) so a
            // manual drag morphs too: once the sheet is pulled past the midpoint toward
            // expanded, the Now Playing block becomes the compact pill.
            let morphMid = (sheet.restingHeight(.controls, total: total)
                            + sheet.restingHeight(.expanded, total: total)) / 2
            let compactNow = sheetH > morphMid

            ZStack(alignment: .top) {
                let bHalfOpen = model.nowPlayingLayout == .b && sheet.level == .controls && !compactNow
                NowPlayingView(
                    display: model.nowPlayingDisplay,
                    compact: compactNow,
                    audioFormat: model.audioFormat,
                    theme: model.nowPlayingTheme,
                    layout: model.nowPlayingLayout,
                    soundControlsHalfOpen: bHalfOpen,
                    lyricFontSize: sheet.level == .controls ? 16 : 20,
                    monitor: monitor)
                    .frame(maxWidth: .infinity)
                    .frame(height: max(120, total - sheetH),
                           alignment: (compactNow || model.nowPlayingLayout == .b) ? .top : .center)
                    .padding(.top, compactNow ? 6 : 0)
                    // B: pull the cover block up into the empty band the top bar leaves
                    // below the device/ON row; lift further when the sheet is half-open.
                    .offset(y: (model.nowPlayingLayout == .b && !compactNow) ? (bHalfOpen ? -52 : -16) : 0)
                    .animation(.smooth(duration: 0.3), value: bHalfOpen)

                // The drag (1:1) + release snap are driven by the tab-bar handle via
                // `sheet`; no implicit `.animation(value:)` here so the layers move
                // together under the model's single explicit snap animation.
                sheetView(total: total)
                    .frame(height: sheetH, alignment: .top)
                    .padding(.horizontal, sheetSideInset)
                    .frame(maxWidth: .infinity)
                    .frame(maxHeight: .infinity, alignment: .bottom)
                    // Stop the sheet at the tab bar's bottom edge instead of the
                    // window edge, so its rounded bottom corners line up with the bar.
                    .padding(.bottom, sheetBottomInset)
                    .zIndex(1)
            }
            // Feed the live content height to the model so the tab-bar handle runs
            // the same snap math.
            .onChange(of: total, initial: true) { sheet.lastTotal = total }
        }
    }

    private let sheetSideInset: CGFloat = 13
    // The tab bar floats 14pt above the window bottom (its own `.padding(.bottom, 14)`
    // in RoomcutAppCanvas) — stop the sheet at the same line so their bottoms align.
    private let sheetBottomInset: CGFloat = 14

    // MARK: Sheet

    private func sheetView(total: CGFloat) -> some View {
        // The glass is ALWAYS rendered — when closed it's simply shorter than the tab
        // bar and tucked behind it (invisible), so it never animates in/out. (The
        // earlier conditional render made Liquid Glass morph in/out → the springy
        // "shake" on close.) Only the contents toggle.
        VStack(spacing: 0) {
            if sheet.level != .minimized {
                // The sheet's OWN handle at its top — drag DOWN here to close.
                sheetGrabber
                ScrollView {
                    VStack(spacing: 16) {
                        if segment == .basic || !sheet.isExpanded {
                            RoomcutMacroControls(
                                model: model,
                                showSummary: sheet.isExpanded,
                                onShowAdvanced: {
                                    withAnimation(sheet.snap) {
                                        segment = .advanced
                                        sheet.level = .expanded
                                    }
                                })
                        } else {
                            AdvancedControls(model: model, meters: model.meters)
                        }

                        if sheet.isExpanded {
                            volumeBar
                            EqPresetPicker(model: model)
                        }
                    }
                    .padding(.horizontal, 18)
                    .padding(.top, 4)
                    .padding(.bottom, (sheet.isExpanded ? 64 : 8) + sheet.tabClear)
                }
                .scrollIndicators(.never)
                // Only bounce when content actually overflows, and NEVER while the
                // sheet itself is being dragged — otherwise shrinking the sheet on a
                // close drag squeezes the scroll view and it springs the content up
                // and down ("발작"). Open grows (no squeeze), so it stays smooth.
                .scrollBounceBehavior(.basedOnSize)
                .scrollDisabled(!sheet.isExpanded || sheet.dragOffset != 0)
            }
        }
        .frame(maxWidth: .infinity)
        .frame(maxHeight: .infinity, alignment: .top)
        // Clip content to the (shrinking) glass so nothing spills/reflows past it
        // during a drag.
        .clipped()
        // Translucent glass so the Cover/Mesh wash shows through.
        .roomcutGlass(.sheet)
        // Fully transparent when CLOSED so nothing peeks out behind the tab bar —
        // the sheet is a wider rounded-rect than the tab bar's capsule, so its bottom
        // corners poked past the bar. Still RENDERED (not removed) so Liquid Glass
        // never morphs in/out (the close "shake"); it just fades.
        .opacity(sheet.level != .minimized || sheet.dragOffset != 0 ? 1 : 0)
        // Basic/Advanced segment — only when expanded, just above the tab bar.
        .overlay(alignment: .bottom) {
            if sheet.isExpanded {
                segmentControl
                    .padding(.bottom, sheet.tabClear + 12)
                    .transition(.opacity.combined(with: .move(edge: .bottom)))
            }
        }
        // EQ Preset pill — surfaced in the half-open (controls) state, dropped into the
        // empty band BELOW the dB readouts and above the tab bar.
        .overlay(alignment: .bottom) {
            if sheet.level == .controls {
                EqPresetPicker(model: model)
                    .padding(.horizontal, 18)
                    .padding(.bottom, sheet.tabClear - 18)
                    .transition(.opacity)
            }
        }
    }

    // The sheet's own handle (only while open): drag DOWN here to close.
    private var sheetGrabber: some View {
        Capsule()
            .fill(Color.secondary.opacity(0.45))
            .frame(width: 36, height: 3)
            .frame(maxWidth: .infinity)
            .frame(height: 22, alignment: .center)
            .contentShape(Rectangle())
            .gesture(
                // GLOBAL coordinate space: the grabber rides on the sheet, so as the
                // drag resizes the sheet the handle moves too — in LOCAL space that
                // motion feeds back into `translation` and the panel oscillates (the
                // "발작" when you pause mid-drag). Global coords track only the mouse.
                DragGesture(minimumDistance: 4, coordinateSpace: .global)
                    .onChanged { sheet.dragChanged($0.translation.height) }
                    .onEnded { sheet.dragEnded(predictedTranslation: $0.predictedEndTranslation.height) }
            )
            .onTapGesture(count: 2) { sheet.toggleExpanded() }
            .accessibilityLabel(L("Sound Controls 시트", "Sound Controls sheet", "サウンドコントロール シート",
                                  "Feuille des contrôles audio", "Sound-Controls-Blatt"))
            .accessibilityHint(L("아래로 끌면 닫힙니다", "Drag down to close", "下にドラッグで閉じる",
                                 "Glisser vers le bas pour fermer", "Zum Schließen nach unten ziehen"))
            .accessibilityAction { sheet.toggleExpanded() }
    }

    // #12 — glass segment with a morphing droplet selection.
    private var segmentControl: some View {
        GlassEffectContainer(spacing: 6) {
            HStack(spacing: 4) {
                ForEach(Segment.allCases, id: \.self) { seg in
                    let selected = segment == seg
                    Button {
                        withAnimation(.smooth(duration: 0.25)) { segment = seg }
                    } label: {
                        Text(seg.displayName)
                            .font(.system(size: 12, weight: selected ? .semibold : .regular))
                            .foregroundStyle(selected ? RoomcutTokens.blue(scheme) : .secondary)
                            .frame(width: 78)
                            .padding(.vertical, 7)
                            .background {
                                if selected {
                                    Capsule().fill(RoomcutTokens.blue(scheme).opacity(0.22))
                                }
                            }
                            .contentShape(Capsule())
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(4)
        }
        .glassEffect(.regular, in: Capsule())
        .clipShape(Capsule())          // trim the glass drop shadow
    }

    // #14 — volume control bar.
    private var volumeBar: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Label(L("Volume", "Volume", "音量", "Volume", "Lautstärke"), systemImage: "speaker.wave.2")
                    .font(.system(size: 11, weight: .semibold))
                    .labelStyle(.titleAndIcon)
                    .foregroundStyle(.secondary)
                Spacer()
                Text(model.hasVolumeControl ? "\(Int((model.volume * 100).rounded()))%" : "—")
                    .font(.system(size: 11, weight: .medium).monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            Slider(value: Binding(get: { model.volume }, set: { model.setVolume($0) }),
                   in: 0...RoomcutViewModel.maxVolume,
                   onEditingChanged: { $0 ? model.beginVolumeEdit() : model.endVolumeEdit() })
                .tint(RoomcutTokens.blue(scheme))
                .environment(\.appearsActive, true)
                .disabled(!model.hasVolumeControl)
        }
    }

}
