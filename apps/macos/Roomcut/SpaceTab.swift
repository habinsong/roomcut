//
// SpaceTab.swift — Phase 7 spatial controls.
//
// Liquid-Glass layout: the 3D field card on top, then ONE settings card that fits
// the window without scrolling. Output and head tracking stay visible; Surround,
// Room and Stage collapse to a single summary row each and open one at a time
// (progressive disclosure), so the tab reads as a short list, not a control wall.
// Glass stays on the controls themselves (segmented capsules, native slider and
// switch knobs); the card is a material, never glass on glass.
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct SpaceTab: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Namespace private var segNS          // shared by the sliding selection pills
    // The open group survives tab switches; "" = everything collapsed.
    @AppStorage("roomcut.space.openSection") private var openSection = ""
    private var accentColor: Color { RoomcutTokens.blue(scheme) }
    private var motion: Animation? { reduceMotion ? nil : .smooth(duration: 0.3) }

    enum SpatialMode: String, CaseIterable, Identifiable {
        case off = "Off", focus = "Focus", widen = "Widen", custom = "Custom"
        var id: String { rawValue }
    }

    private enum Section: String { case surround, room, stage }

    var body: some View {
        // Settings' tighter bottom inset: the tallest open group (Stage with
        // Crossfeed) then still ends above the tab bar with nothing to scroll.
        RoomcutTabScreen(bottomPadding: 80) {
            RoomcutSection("") {
                // Every control on this tab feeds the picture, so it always
                // shows the setting rather than a generic stage.
                SpatialFieldView(width: model.spatialWidth,
                                 center: model.centerFocus,
                                 crossfeed: model.crossfeed,
                                 room: model.roomReduce,
                                 headphone: model.spatialOutputIsHeadphone,
                                 surroundType: model.surroundType,
                                 ambience: model.spatialSurroundOn,
                                 roomType: model.roomType,
                                 centerWidth: model.centerWidth,
                                 surroundDepth: model.surroundDepth,
                                 roomAmount: model.roomAmount,
                                 balance: model.balance,
                                 headYaw: model.headYawDegrees,
                                 tracking: model.headTrackingOn,
                                 accent: accentColor)
                    .padding(.horizontal, 10).padding(.vertical, 10)
                    .opacity(model.spatialAvailable ? 1 : 0.4)
                    .overlay(alignment: .topTrailing) { resetButton.padding(12) }
            }

            RoomcutSection("") {
                outputPicker
                Divider().opacity(0.4)
                spatialRows
                RoomcutDivider()
                // Balance is the device's L/R output position, not a spatial DSP
                // param, so it stays usable even without Spatial.
                balanceRow
            }
            .animation(motion, value: model.spatialOutputIsHeadphone)
            .animation(motion, value: model.headTrackingAvailable)
            .animation(motion, value: model.virtualRoomAvailable)

            if model.status.reachable && !model.spatialAvailable {
                Text(L("현재 실행 중인 엔진이 Spatial을 지원하지 않습니다.",
                       "The running engine does not support Spatial.",
                       "実行中のエンジンは Spatial に対応していません。",
                       "Le moteur en cours d'exécution ne prend pas en charge Spatial.",
                       "Die laufende Engine unterstützt Spatial nicht."))
                    .font(.system(size: 12)).foregroundStyle(.secondary).padding(.leading, 6)
            }
        }
        // Everything fits the window; only bounce if a long locale overflows it.
        .scrollBounceBehavior(.basedOnSize)
        .onDisappear { model.endParameterEdit() }
    }

    // MARK: Always-visible controls

    private var outputPicker: some View {
        glassSegmented([L("Speaker", "Speaker", "スピーカー", "Haut-parleur", "Lautsprecher"),
                        L("Headphone", "Headphone", "ヘッドフォン", "Casque", "Kopfhörer")],
                       selected: model.spatialOutputIsHeadphone ? 1 : 0,
                       group: "output") { idx in
            // Re-apply the active preset under the NEW output so its
            // crossfeed follows the mode (speaker XTC ↔ headphone crossfeed)
            // instead of leaving a value that's wrong for the other system.
            let active = inferredMode
            model.setSpatialOutput(headphone: idx == 1)
            if active != .custom, let v = presetValues(active) {
                model.setSpatialValues(width: v.width, centerFocus: v.center,
                                       crossfeed: v.crossfeed, roomReduce: v.room)
            }
        }
        .padding(.horizontal, 12).padding(.vertical, 10)
        .disabled(!model.spatialAvailable)
        .opacity(model.spatialAvailable ? 1 : 0.4)
    }

    private var spatialRows: some View {
        Group {
            disclosure(.surround, L("Surround", "Surround", "サラウンド", "Surround", "Surround"),
                       summary: surroundLabel(model.surroundChoice)) { surroundControls }

            // Head tracking — headphones with motion sensors only. The engine
            // anchors the virtual speakers to the screen, so the stage stays put
            // when the listener turns. Binary, so a switch, not a segment.
            if model.headTrackingAvailable {
                RoomcutDivider()
                headTrackingRow
            }

            // Virtual room on either output. The engine builds a different room
            // for each, so the choice follows the Speaker/Headphone switch.
            if model.virtualRoomAvailable {
                RoomcutDivider()
                disclosure(.room, L("Room", "Room", "ルーム", "Salle", "Virtueller Raum"),
                           summary: roomLabels[min(3, max(0, Int(model.roomType.rounded())))]) { roomControls }
            }

            RoomcutDivider()
            disclosure(.stage, L("Stage", "Stage", "ステージ", "Scène", "Bühne"),
                       summary: modeLabel(inferredMode)) { stageControls }
        }
        .disabled(!model.spatialAvailable)
        .opacity(model.spatialAvailable ? 1 : 0.4)
    }

    private var headTrackingRow: some View {
        HStack(spacing: 10) {
            Text(L("Head Tracking", "Head Tracking", "ヘッドトラッキング", "Suivi de tête", "Kopfverfolgung"))
                .font(.system(size: 13)).foregroundStyle(RoomcutTokens.textPrimary(scheme))
            Spacer(minLength: 8)
            if model.headTrackingOn {
                recentreButton.transition(.opacity)
            }
            Toggle("", isOn: Binding(get: { model.headTrackingOn }, set: { model.setHeadTracking($0) }))
                .labelsHidden()
                .toggleStyle(.switch)
                .controlSize(.small)
                .tint(accentColor)
                .accessibilityLabel(L("Head Tracking", "Head Tracking", "ヘッドトラッキング",
                                      "Suivi de tête", "Kopfverfolgung"))
        }
        .padding(.horizontal, 16).padding(.vertical, 8)
        .animation(motion, value: model.headTrackingOn)
    }

    // Balance / pan: centre-anchored L↔R, backed by the device's per-channel volume.
    // Begin/endEdit gate the poll so an external (Audio MIDI Setup) change is mirrored
    // when idle but doesn't snap the thumb back mid-drag — same contract as volume.
    private var balanceRow: some View {
        let v = Int((model.balance * 100).rounded())
        return sliderRow(L("Balance", "Balance", "バランス", "Balance", "Balance"),
                         value: model.balance, in: -1...1,
                         display: v == 0 ? "C" : (v < 0 ? "L\(-v)" : "R\(v)"),
                         primary: true,
                         editing: { $0 ? model.beginBalanceEdit() : model.endBalanceEdit() }) {
            model.setBalance($0)
        }
        .disabled(!model.hasBalanceControl)
        .opacity(model.hasBalanceControl ? 1 : 0.4)
    }

    // MARK: Disclosed groups

    // Surround is one control. What it can offer depends on the output:
    // headphones can render a real virtual layout, while speakers get the same
    // decomposition folded back and thrown wide (no back channel exists).
    private var surroundControls: some View {
        VStack(spacing: 0) {
            glassSegmented(model.surroundChoices.map(surroundLabel),
                           selected: model.surroundChoices.firstIndex(of: model.surroundChoice) ?? 0,
                           group: "surround") { idx in
                let choices = model.surroundChoices
                guard idx < choices.count else { return }
                model.setSurroundChoice(choices[idx])
            }
            .padding(.horizontal, 12).padding(.bottom, 4)

            // Only an actual 5.1/7.1 layout has a centre and surrounds to steer.
            if model.upmixAvailable && model.surroundType >= 2 {
                if model.centerWidthApplies {
                    sliderRow(L("Center Width", "Center Width", "センター幅", "Largeur centrale", "Center-Breite"),
                              value: model.centerWidth, in: 0...100) { model.setCenterWidth($0) }
                }
                sliderRow(L("Surround Depth", "Surround Depth", "サラウンド深度", "Profondeur surround", "Surround-Tiefe"),
                          value: model.surroundDepth, in: 0...100) { model.setSurroundDepth($0) }
            }
        }
        .animation(motion, value: model.surroundType)
        .animation(motion, value: model.surroundChoices)
    }

    private var roomControls: some View {
        VStack(spacing: 0) {
            glassSegmented(roomLabels,
                           selected: Int(model.roomType.rounded()),
                           group: "room") { idx in
                model.setRoomType(Double(idx))
            }
            .padding(.horizontal, 12).padding(.bottom, 4)

            // The room's own level, shown only while a room is actually on.
            if model.roomType >= 1 {
                sliderRow(L("Amount", "Amount", "ルーム量", "Niveau", "Anteil"),
                          value: model.roomAmount, in: 0...100) { model.setRoomAmount($0) }
            }
        }
        .animation(motion, value: model.roomType >= 1)
    }

    // Mode presets sit directly above the sliders they drive: picking Focus moves
    // them, moving one reveals Custom.
    // Damping / Space / Center keep the engine's 0…200 (Space ±200) reach, but the
    // rows read HALF of it — 0…100 and ±100 — and double on the way out.
    private var stageControls: some View {
        VStack(spacing: 0) {
            glassSegmented(visibleModes.map { modeLabel($0) },
                           selected: visibleModes.firstIndex(of: inferredMode) ?? 0,
                           group: "mode") { idx in
                modeSelection.wrappedValue = visibleModes[idx]
            }
            .animation(motion, value: visibleModes)
            .padding(.horizontal, 12).padding(.bottom, 4)

            sliderRow(L("Space", "Space", "空間", "Espace", "Raum"),
                      value: model.spatialWidth / 2, in: -100...100) { model.setSpatialWidth($0 * 2) }
            sliderRow(L("Center", "Center", "センター", "Centre", "Mitte"),
                      value: model.centerFocus / 2, in: 0...100) { model.setCenterFocus($0 * 2) }
            sliderRow(L("Damping", "Damping", "ダンピング", "Amortissement", "Dämpfung"),
                      value: model.roomReduce / 2, in: 0...100) { model.setRoomReduce($0 * 2) }
            // While head tracking or the upmix is placing speakers itself, the
            // engine zeroes crossfeed — it is a third, fixed-head version of the
            // same job. Hiding it beats showing a control that does nothing.
            if model.crossfeedActive {
                sliderRow(model.spatialOutputIsHeadphone ? "Crossfeed" : "Crosstalk 3D",
                          value: model.crossfeed, in: 0...100) { model.setCrossfeed($0) }
            }
        }
        .animation(motion, value: model.crossfeedActive)
    }

    // MARK: Building blocks

    // A summary row that opens its group underneath. One group at a time keeps
    // the card inside the window; the summary fades while its own control shows it.
    private func disclosure<Content: View>(_ section: Section, _ title: String, summary: String,
                                           @ViewBuilder content: () -> Content) -> some View {
        let open = openSection == section.rawValue
        return VStack(spacing: 0) {
            Button {
                withAnimation(motion) { openSection = open ? "" : section.rawValue }
            } label: {
                HStack(spacing: 8) {
                    Text(title)
                        .font(.system(size: 13))
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                    Spacer(minLength: 8)
                    Text(summary)
                        .font(.system(size: 13))
                        .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                        .opacity(open ? 0 : 1)
                    Image(systemName: "chevron.right")
                        .font(.system(size: 11, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                        .rotationEffect(.degrees(open ? 90 : 0))
                }
                .padding(.horizontal, 16).padding(.vertical, 9)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(title)
            .accessibilityValue(summary)
            .accessibilityHint(open ? L("접기", "Collapse", "折りたたむ", "Réduire", "Einklappen")
                                    : L("펼치기", "Expand", "展開", "Développer", "Aufklappen"))

            if open {
                content()
                    .padding(.bottom, 8)
                    .transition(.opacity.combined(with: .offset(y: -6)))
            }
        }
    }

    // One-line slider row: label, native (glass-knob) slider, value.
    private func sliderRow(_ title: String, value: Double, in range: ClosedRange<Double>,
                           display: String? = nil, primary: Bool = false,
                           editing: ((Bool) -> Void)? = nil,
                           _ set: @escaping (Double) -> Void) -> some View {
        HStack(spacing: 10) {
            Text(title)
                .font(.system(size: primary ? 13 : 12))
                .foregroundStyle(primary ? RoomcutTokens.textPrimary(scheme) : RoomcutTokens.textSecondary(scheme))
                .lineLimit(1).minimumScaleFactor(0.8)
                .frame(width: 100, alignment: .leading)
            Slider(value: Binding(get: { value }, set: set), in: range,
                   onEditingChanged: editing ?? { $0 ? model.beginParameterEdit() : model.endParameterEdit() })
                .controlSize(.small)
                .tint(accentColor)
                .accessibilityLabel(title)
            Text(display ?? "\(Int(value.rounded()))")
                .font(.system(size: 12, weight: .medium).monospacedDigit())
                .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                .frame(width: 32, alignment: .trailing)
        }
        .padding(.horizontal, 16).padding(.vertical, primary ? 9 : 4)
    }

    // Small circular glass reset, tucked into the field's top-right corner.
    private var resetButton: some View {
        Button { model.setSpatialValues(width: 0, centerFocus: 0, crossfeed: 0, roomReduce: 0) } label: {
            Image(systemName: "arrow.counterclockwise")
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(accentColor)
                .frame(width: 30, height: 30)
                .glassEffect(.regular, in: Circle())
                .clipShape(Circle())          // trim the glass drop shadow
                .contentShape(Circle())
        }
        .buttonStyle(.plain)
        .disabled(!model.spatialAvailable)
        .help(L("Spatial 초기화", "Reset Spatial", "Spatial をリセット", "Réinitialiser Spatial", "Spatial zurücksetzen"))
        .accessibilityLabel(L("Spatial 초기화", "Reset Spatial", "Spatial をリセット", "Réinitialiser Spatial", "Spatial zurücksetzen"))
    }

    // "Straight ahead is where I am looking now." A gyro drifts, so this is the
    // control a listener reaches for when the stage has crept off-centre. A flat
    // capsule, not glass: it sits in the card next to the switch's own glass knob.
    private var recentreButton: some View {
        Button { model.recentreHead() } label: {
            Image(systemName: "scope")
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(accentColor)
                .padding(.horizontal, 9).padding(.vertical, 5)
                .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .background(Capsule().fill(.quaternary))
        .help(L("정면 재설정", "Recentre", "正面をリセット", "Recentrer", "Neu zentrieren"))
        .accessibilityLabel(L("정면 재설정", "Recentre", "正面をリセット", "Recentrer", "Neu zentrieren"))
    }

    private func surroundLabel(_ choice: RoomcutViewModel.SurroundChoice) -> String {
        switch choice {
        case .off: return L("Off", "Off", "オフ", "Désactivé", "Aus")
        case .ambience: return L("Ambience", "Ambience", "アンビエンス", "Ambiance", "Ambiente")
        case .virtual51:
            // On speakers there is no rear, so the same setting is named for what
            // it actually does there rather than for a layout it cannot produce.
            return model.spatialOutputIsHeadphone ? "5.1"
                : L("Wide", "Wide", "ワイド", "Large", "Breit")
        case .virtual71: return "7.1"
        }
    }

    // Virtual room choices, in the same order as the engine's room table
    // (0 = off, 1 = Studio, 2 = Living Room, 3 = Hall).
    private var roomLabels: [String] {
        [L("Off", "Off", "オフ", "Désactivé", "Aus"),
         L("studio", "studio", "スタジオ", "studio", "studio"),
         L("live", "live", "リビング", "live", "live"),
         L("hall", "hall", "ホール", "hall", "hall")]
    }

    // Display label for a spatial mode (English base + ja/fr/de translations); the
    // SpatialMode rawValue stays the identity used for selection/inference.
    private func modeLabel(_ m: SpatialMode) -> String {
        switch m {
        case .off:    return L("Off", "Off", "オフ", "Désactivé", "Aus")
        case .focus:  return L("Focus", "Focus", "フォーカス", "Focus", "Fokus")
        case .widen:  return L("Widen", "Widen", "ワイド", "Élargir", "Verbreitern")
        case .custom: return L("Custom", "Custom", "カスタム", "Personnalisé", "Benutzerdef.")
        }
    }

    // Liquid-Glass segmented control (same recipe as the Settings theme picker): a glass
    // capsule with a subtle monochrome pill that SLIDES between options (matchedGeometry).
    // `group` keeps each control's pill independent; `.clipShape` trims the glass drop
    // shadow so it sits flat on the card.
    private func glassSegmented(_ labels: [String], selected: Int, group: String,
                                _ select: @escaping (Int) -> Void) -> some View {
        GlassEffectContainer(spacing: 5) {
            HStack(spacing: 4) {
                ForEach(Array(labels.enumerated()), id: \.offset) { idx, label in
                    let isSel = idx == selected
                    Button { select(idx) } label: {
                        Text(label)
                            .font(.system(size: 12, weight: isSel ? .semibold : .regular))
                            .foregroundStyle(isSel ? RoomcutTokens.textPrimary(scheme)
                                             : RoomcutTokens.textSecondary(scheme))
                            .lineLimit(1).minimumScaleFactor(0.8)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 7)
                            .background {
                                if isSel {
                                    Capsule()
                                        .fill(accentColor.opacity(0.22))
                                        .matchedGeometryEffect(id: group, in: segNS)
                                }
                            }
                            .contentShape(Capsule())
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(4)
            .animation(reduceMotion ? nil : .spring(response: 0.32, dampingFraction: 0.86), value: selected)
        }
        .glassEffect(.regular, in: Capsule())
        .clipShape(Capsule())
    }

    // Custom is hidden while a named mode (Off/Focus/Widen) is active; it fades in
    // only once the sliders have moved the state off a preset.
    private var visibleModes: [SpatialMode] {
        inferredMode == .custom ? SpatialMode.allCases : [.off, .focus, .widen]
    }

    // Preset targets, re-tuned for the doubled-strength / 3-band-shuffler DSP and made
    // OUTPUT-AWARE: crossfeed means opposite things on the two systems, so each preset
    // sets it per mode (speaker XTC widens; headphone crossfeed narrows/naturalises).
    //   • Widen  → speakers add XTC for an out-of-speaker stage; headphones keep full
    //              separation (crossfeed 0), widening via the M/S side only.
    //   • Focus  → tight, centred, dry image; speakers keep XTC off (it would widen),
    //              headphones add crossfeed to naturalise the hard separation.
    private func presetValues(_ mode: SpatialMode)
        -> (width: Double, center: Double, crossfeed: Double, room: Double)? {
        let headphone = model.spatialOutputIsHeadphone
        switch mode {
        case .off:    return (0, 0, 0, 0)
        case .focus:  return (-40, 40, headphone ? 25 : 0, 50)
        case .widen:  return (50, 0, headphone ? 0 : 35, 0)
        case .custom: return nil
        }
    }

    private var modeSelection: Binding<SpatialMode> {
        Binding(get: { inferredMode }, set: { mode in
            guard model.spatialAvailable, let v = presetValues(mode) else { return }
            model.setSpatialValues(width: v.width, centerFocus: v.center,
                                   crossfeed: v.crossfeed, roomReduce: v.room)
        })
    }

    private var inferredMode: SpatialMode {
        func near(_ a: Double, _ b: Double) -> Bool { abs(a - b) < 0.5 }
        func matches(_ mode: SpatialMode) -> Bool {
            guard let v = presetValues(mode) else { return false }
            return near(model.spatialWidth, v.width) && near(model.centerFocus, v.center)
                && near(model.crossfeed, v.crossfeed) && near(model.roomReduce, v.room)
        }
        if matches(.off) { return .off }
        if matches(.focus) { return .focus }
        if matches(.widen) { return .widen }
        return .custom
    }
}
