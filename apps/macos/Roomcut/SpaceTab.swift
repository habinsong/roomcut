//
// SpaceTab.swift — Phase 7 spatial controls.
//
// Liquid-Glass layout: the 3D field card on top, then ONE settings card that
// reaches down to the tab bar, leaving above the bar the same gap the bar keeps
// below itself. The card reads top to bottom in the order a listener decides:
//
//   output (Speaker / Headphone) → a whole-scene preset → the three choices the
//   preset is made of (Surround, Room, Stage), each visible at once as its own
//   row → head tracking → the fine sliders, folded away → Balance at the foot.
//
// It used to hide Surround, Room and Stage behind three disclosure rows, only
// one open at a time, so the current setting was a summary word and every change
// took two taps. Sliders follow Home's Limiter layout: name and value on one
// line, a full-width slider under them, so the track is centred in the card.
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
    @Environment(\.roomcutTabBarClearance) private var tabBarClearance
    @Namespace private var segNS          // shared by the sliding selection pills
    // Whether the fine sliders are showing; survives tab switches.
    @AppStorage("roomcut.space.fineTuneOpen") private var fineTuneOpen = false
    private var accentColor: Color { RoomcutTokens.blue(scheme) }
    private var motion: Animation? { reduceMotion ? nil : .smooth(duration: 0.3) }

    private static let topPadding: CGFloat = 6
    private static let labelWidth: CGFloat = 70

    enum SpatialMode: String, CaseIterable, Identifiable {
        case off = "Off", focus = "Focus", widen = "Widen", custom = "Custom"
        var id: String { rawValue }
    }

    var body: some View {
        GeometryReader { geo in
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    RoomcutSection("") { fieldPicture }
                    RoomcutSection("") { settings }
                        .frame(maxHeight: .infinity, alignment: .top)
                }
                // At least the visible height, so the settings card stretches to
                // the tab bar; taller content (fine sliders open) scrolls.
                .frame(minHeight: max(0, geo.size.height - Self.topPadding - tabBarClearance), alignment: .top)
                .padding(.horizontal, 16).padding(.top, Self.topPadding).padding(.bottom, tabBarClearance)
            }
            .scrollIndicators(.never)
            .scrollBounceBehavior(.basedOnSize)
        }
        .onDisappear { model.endParameterEdit() }
    }

    // Every control on this tab feeds the picture, so it always shows the
    // setting rather than a generic stage.
    private var fieldPicture: some View {
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
                         tracking: model.headTrackingActive,
                         accent: accentColor)
            .padding(10)
            .opacity(model.spatialAvailable ? 1 : 0.4)
    }

    private var settings: some View {
        VStack(spacing: 0) {
            if model.status.reachable && !model.spatialAvailable {
                Text(L("현재 실행 중인 엔진이 Spatial을 지원하지 않습니다.",
                       "The running engine does not support Spatial.",
                       "実行中のエンジンは Spatial に対応していません。",
                       "Le moteur en cours d'exécution ne prend pas en charge Spatial.",
                       "Die laufende Engine unterstützt Spatial nicht."))
                    .font(.system(size: 12)).foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 16).padding(.top, 12)
            }

            stretched {
                VStack(spacing: 8) {
                    outputPicker
                    SpacePresetPicker(model: model)
                }
                .padding(.horizontal, 12).padding(.vertical, 10)
                .disabled(!model.spatialAvailable)
                .opacity(model.spatialAvailable ? 1 : 0.4)
            }

            RoomcutDivider()
            choices
            RoomcutDivider()
            stretched { fineTune }

            // Balance is the device's L/R output position, not a spatial DSP
            // param, so it stays usable even without Spatial.
            RoomcutDivider()
            stretched { balanceSlider.padding(.vertical, 4) }
        }
        .frame(maxHeight: .infinity, alignment: .top)
        .animation(motion, value: model.spatialOutputIsHeadphone)
        .animation(motion, value: model.headTrackingAvailable)
        .animation(motion, value: model.virtualRoomAvailable)
        .animation(motion, value: fineTuneOpen)
    }

    // MARK: Output and the three scene choices

    private var outputPicker: some View {
        glassSegmented([L("Speaker", "Speaker", "スピーカー", "Haut-parleur", "Lautsprecher"),
                        L("Headphone", "Headphone", "ヘッドフォン", "Casque", "Kopfhörer")],
                       selected: model.spatialOutputIsHeadphone ? 1 : 0,
                       group: "output") { idx in
            let headphone = idx == 1
            guard headphone != model.spatialOutputIsHeadphone else { return }
            // Each output comes back to the preset it was left on. Without one,
            // re-apply the stage mode under the NEW output so its crossfeed
            // follows (speaker XTC ↔ headphone crossfeed) instead of leaving a
            // value that is wrong for the other system.
            let active = inferredMode
            if !model.switchSpatialOutput(headphone: headphone),
               active != .custom, let v = presetValues(active) {
                model.setSpatialValues(width: v.width, centerFocus: v.center,
                                       crossfeed: v.crossfeed, roomReduce: v.room)
            }
        }
    }

    // Each row takes an equal share of whatever height the card has left, so the
    // list spreads evenly down to Balance instead of leaving a hole above it.
    // With Fine Tune open there is no height left and the rows sit tight.
    private func stretched<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)
            // The row keeps its own height; only the spare height is shared.
            // Without this the rows were squeezed and their labels shrank.
            content().fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 0)
        }
    }

    // A Group, not a stack: its rows are the card's own children, so each takes
    // its share of the spare height like every other row.
    private var choices: some View {
        Group {
            // What Surround can offer depends on the output: headphones render a
            // real virtual layout, speakers get the same decomposition thrown wide.
            stretched {
                choiceRow(L("Surround", "Surround", "サラウンド", "Surround", "Surround"),
                          model.surroundChoices.map(surroundLabel),
                          selected: model.surroundChoices.firstIndex(of: model.surroundChoice) ?? 0,
                          group: "surround") { idx in
                    let choices = model.surroundChoices
                    guard idx < choices.count else { return }
                    model.setSurroundChoice(choices[idx])
                }
            }

            // Virtual room on either output. The engine builds a different room
            // for each, so the choice follows the Speaker/Headphone switch.
            if model.virtualRoomAvailable {
                stretched {
                    choiceRow(L("Room", "Room", "ルーム", "Salle", "Raum"), roomLabels,
                              selected: min(3, max(0, Int(model.roomType.rounded()))),
                              group: "room") { model.setRoomType(Double($0)) }
                }
            }

            // Mode presets for the sliders under Fine Tune: picking Focus moves
            // them, moving one reveals Custom.
            stretched {
                choiceRow(L("Stage", "Stage", "ステージ", "Scène", "Bühne"),
                          visibleModes.map { modeLabel($0) },
                          selected: visibleModes.firstIndex(of: inferredMode) ?? 0,
                          group: "mode") { idx in
                    modeSelection.wrappedValue = visibleModes[idx]
                }
                .animation(motion, value: visibleModes)
            }

            // Head tracking — headphones with motion sensors only. The engine
            // anchors the virtual speakers to the screen, so the stage stays put
            // when the listener turns. Binary, so a switch, not a segment.
            if model.headTrackingAvailable {
                stretched { headTrackingRow }
            }
        }
        .disabled(!model.spatialAvailable)
        .opacity(model.spatialAvailable ? 1 : 0.4)
        .animation(motion, value: model.surroundChoices)
    }

    private func choiceRow(_ title: String, _ labels: [String], selected: Int, group: String,
                           _ select: @escaping (Int) -> Void) -> some View {
        HStack(spacing: 10) {
            Text(title)
                .font(.system(size: 13))
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                .lineLimit(1).minimumScaleFactor(0.8)
                .frame(width: Self.labelWidth, alignment: .leading)
            glassSegmented(labels, selected: selected, group: group, select)
                .accessibilityLabel(title)
        }
        .padding(.horizontal, 16).padding(.vertical, 5)
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

    // MARK: Fine tune

    private var fineTune: some View {
        VStack(spacing: 0) {
            Button {
                withAnimation(motion) { fineTuneOpen.toggle() }
            } label: {
                HStack(spacing: 8) {
                    Text(L("Fine Tune", "Fine Tune", "微調整", "Réglage fin", "Feinabstimmung"))
                        .font(.system(size: 13))
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                    Spacer(minLength: 8)
                    Image(systemName: "chevron.right")
                        .font(.system(size: 11, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                        .rotationEffect(.degrees(fineTuneOpen ? 90 : 0))
                }
                .padding(.horizontal, 16).padding(.vertical, 10)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityHint(fineTuneOpen ? L("접기", "Collapse", "折りたたむ", "Réduire", "Einklappen")
                                            : L("펼치기", "Expand", "展開", "Développer", "Aufklappen"))

            if fineTuneOpen {
                fineTuneSliders
                    .padding(.bottom, 6)
                    .transition(.opacity.combined(with: .offset(y: -6)))
            }
        }
        .disabled(!model.spatialAvailable)
        .opacity(model.spatialAvailable ? 1 : 0.4)
    }

    // Only what applies to the current choices: a room's level while a room is
    // on, the upmix's steering while there is an upmix, crossfeed while the
    // engine renders it.
    // Damping / Space / Center keep the engine's 0…200 (Space ±200) reach, but the
    // sliders read HALF of it — 0…100 and ±100 — and double on the way out.
    private var fineTuneSliders: some View {
        VStack(spacing: 0) {
            if model.virtualRoomAvailable && model.roomType >= 1 {
                slider(L("Room Amount", "Room Amount", "ルーム量", "Niveau de salle", "Raumanteil"),
                       value: model.roomAmount, in: 0...100) { model.setRoomAmount($0) }
            }
            if model.upmixAvailable && model.surroundType >= 2 {
                if model.centerWidthApplies {
                    slider(L("Center Width", "Center Width", "センター幅", "Largeur centrale", "Center-Breite"),
                           value: model.centerWidth, in: 0...100) { model.setCenterWidth($0) }
                }
                if model.surroundDepthApplies {
                    slider(L("Surround Depth", "Surround Depth", "サラウンド深度", "Profondeur surround", "Surround-Tiefe"),
                           value: model.surroundDepth, in: 0...100) { model.setSurroundDepth($0) }
                }
            }
            slider(L("Space", "Space", "空間", "Espace", "Raum"),
                   value: model.spatialWidth / 2, in: -100...100) { model.setSpatialWidth($0 * 2) }
            slider(L("Center", "Center", "センター", "Centre", "Mitte"),
                   value: model.centerFocus / 2, in: 0...100) { model.setCenterFocus($0 * 2) }
            slider(L("Damping", "Damping", "ダンピング", "Amortissement", "Dämpfung"),
                   value: model.roomReduce / 2, in: 0...100) { model.setRoomReduce($0 * 2) }
            // While head tracking or the upmix is placing speakers itself, the
            // engine zeroes crossfeed — it is a third, fixed-head version of the
            // same job. Hiding it beats showing a control that does nothing.
            if model.crossfeedActive {
                slider(model.spatialOutputIsHeadphone ? "Crossfeed" : "Crosstalk 3D",
                       value: model.crossfeed, in: 0...100) { model.setCrossfeed($0) }
            }
            // Apple's renderer or the built-in one — where both can play, so A/B
            // can put one on each side.
            if model.bedRendererChoiceAvailable {
                choiceRow(L("Renderer", "Renderer", "レンダラー", "Moteur", "Renderer"),
                          RoomcutViewModel.BedRendererChoice.allCases.map(bedRendererLabel),
                          selected: model.bedRendererChoice.rawValue,
                          group: "bed-renderer") { idx in
                    guard let choice = RoomcutViewModel.BedRendererChoice(rawValue: idx) else { return }
                    model.setBedRendererChoice(choice)
                }
                .padding(.top, 4)
            }
        }
        .animation(motion, value: model.crossfeedActive)
        .animation(motion, value: model.surroundType)
        .animation(motion, value: model.roomType >= 1)
        .animation(motion, value: model.bedRendererChoiceAvailable)
    }

    // MARK: Balance

    // Balance / pan: centre-anchored L↔R, backed by the device's per-channel volume.
    // Begin/endEdit gate the poll so an external (Audio MIDI Setup) change is mirrored
    // when idle but doesn't snap the thumb back mid-drag — same contract as volume.
    private var balanceSlider: some View {
        let v = Int((model.balance * 100).rounded())
        return slider(L("Balance", "Balance", "バランス", "Balance", "Balance"),
                      value: model.balance, in: -1...1,
                      display: v == 0 ? "C" : (v < 0 ? "L\(-v)" : "R\(v)"),
                      editing: { $0 ? model.beginBalanceEdit() : model.endBalanceEdit() }) {
            model.setBalance($0)
        }
        .disabled(!model.hasBalanceControl)
        .opacity(model.hasBalanceControl ? 1 : 0.4)
    }

    // MARK: Building blocks

    // Home's Limiter layout: the name left and the value right on one line, the
    // slider full width beneath, so all three share the same edges.
    private func slider(_ title: String, value: Double, in range: ClosedRange<Double>,
                        display: String? = nil,
                        editing: ((Bool) -> Void)? = nil,
                        _ set: @escaping (Double) -> Void) -> some View {
        let shown = display ?? "\(Int(value.rounded()))"
        return VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).foregroundStyle(RoomcutTokens.textSecondary(scheme))
                Spacer()
                Text(shown).monospacedDigit()
                    .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            }
            .font(.callout)
            Slider(value: Binding(get: { value }, set: set), in: range,
                   onEditingChanged: editing ?? { $0 ? model.beginParameterEdit() : model.endParameterEdit() })
                .tint(accentColor)
                .accessibilityLabel(title)
                .accessibilityValue(shown)
        }
        .padding(.horizontal, 16).padding(.vertical, 5)
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

    private func bedRendererLabel(_ choice: RoomcutViewModel.BedRendererChoice) -> String {
        switch choice {
        case .system: return "Apple"
        case .builtIn: return L("Built-in", "Built-in", "内蔵", "Intégré", "Integriert")
        }
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
                            .lineLimit(1).minimumScaleFactor(0.75)
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
                    .accessibilityAddTraits(isSel ? [.isButton, .isSelected] : .isButton)
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

    // The Stage row's named settings live with the presets (SpaceStage), so a
    // preset and the row always agree on what Focus and Widen are.
    private func presetValues(_ mode: SpatialMode)
        -> (width: Double, center: Double, crossfeed: Double, room: Double)? {
        let stage: SpaceStage
        switch mode {
        case .off:    stage = .off
        case .focus:  stage = .focus
        case .widen:  stage = .widen
        case .custom: return nil
        }
        let v = stage.values(headphone: model.spatialOutputIsHeadphone)
        return (v.width, v.centerFocus, v.crossfeed, v.roomReduce)
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
