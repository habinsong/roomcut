//
// RoomcutMacroControls.swift — the Basic tab's macro row: Bass / Vocal /
// Clarity rendered as circular arc knobs (the design mockup), plus a small EQ
// summary curve with a jump to Advanced.
//
// Each macro maps a value in [-1, 1] onto a few EQ bands via the pure
// RoomcutMacros helper. Interaction: drag the knob up to raise / down to lower;
// click the dB readout to type an exact value. Room/Space spatial macros live in
// the Space tab (Phase 7), not here.
//
import SwiftUI
import AppKit
import RoomcutCore
import RoomcutPresentationCore

private extension EqMacro {
    var iconName: String {
        switch self {
        case .bass:    return "waveform"
        case .warmth:  return "thermometer.medium"
        case .vocal:   return "mic"
        case .clarity: return "slider.horizontal.3"
        case .air:     return "wind"
        }
    }
}

struct RoomcutMacroControls: View {
    @ObservedObject var model: RoomcutViewModel
    var showSummary: Bool = true
    var onShowAdvanced: (() -> Void)?

    // Macro positions live on the model (model.macroValues) so they persist when
    // this view is recreated on Basic↔Advanced or bottom-tab switches.
    @Environment(\.colorScheme) private var scheme

    // dB shown for a normalized value (±6 dB at the extremes).
    private func db(_ v: Double) -> Double { v * 6.0 }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(alignment: .top, spacing: 0) {
                ForEach(EqMacro.allCases, id: \.self) { macro in
                    macroKnob(macro).frame(maxWidth: .infinity)
                }
            }
            .padding(.bottom, 10)
            if showSummary {
                eqSummary
            }
        }
        .disabled(!model.status.reachable)
    }

    private func macroKnob(_ macro: EqMacro) -> some View {
        let value = model.macroValues[macro] ?? 0
        let intensity = 0.28 + min(1, abs(value)) * 0.72
        // Each change moves only this macro's bands, by the delta from its
        // previous position (model.setMacro) — no baseline snapshot, no
        // double-counting, and the knob always matches the engine EQ.
        return MacroKnob(
            title: macro.title,
            icon: macro.iconName,
            color: RoomcutTokens.blue(scheme).opacity(intensity),
            value: value,
            db: db(value),
            beginEdit: {},
            onChange: { model.setMacro(macro, normalized: $0) },
            onSetDb: { model.setMacro(macro, normalized: $0 / 6.0) },
            onEditingEnded: {}
        )
    }

    private var eqSummary: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(L("EQ SUMMARY", "EQ SUMMARY", "EQ サマリー", "RÉSUMÉ EQ", "EQ-ÜBERSICHT"))
                    .font(.system(size: 10, weight: .semibold))
                    .tracking(1.2)
                    .foregroundStyle(.secondary)
                Spacer()
                if let onShowAdvanced {
                    Button(action: onShowAdvanced) {
                        Text(L("View", "View", "表示", "Voir", "Anzeigen"))
                            .font(.system(size: 11, weight: .medium))
                            .foregroundStyle(RoomcutTokens.blue(scheme))
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(L("고급 컨트롤 열기", "Open advanced controls", "詳細コントロールを開く",
                                          "Ouvrir les contrôles avancés", "Erweiterte Steuerung öffnen"))
                }
            }
            EqMiniCurve(gains: model.eqGainsDb, accent: RoomcutTokens.blue(scheme), scheme: scheme)
                .frame(height: 48)
        }
    }
}

// MARK: - EQ preset selector (native glass, no accent tint)

struct EqPresetPicker: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    @State private var showTree = false
    @State private var saveName = ""

    var body: some View {
        HStack(spacing: 8) {
            Button(action: resetToFlat) {
                Image(systemName: "arrow.uturn.left")
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                    .frame(width: 24, height: 24)
                    .contentShape(Circle())
            }
            .buttonStyle(.plain)
            .help(L("Flat으로 초기화", "Reset to Flat", "Flat にリセット", "Réinitialiser à Flat", "Auf Flat zurücksetzen"))
            .accessibilityLabel(L("EQ 초기화", "Reset EQ", "EQ をリセット", "Réinitialiser l'EQ", "EQ zurücksetzen"))

            Button { showTree = true } label: {
                HStack(spacing: 6) {
                    Text(L("EQ Preset", "EQ Preset", "EQ プリセット", "Préréglage EQ", "EQ-Preset"))
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)
                    Spacer(minLength: 6)
                    Text(model.currentPresetName)
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                        .lineLimit(1)
                        .minimumScaleFactor(0.72)
                    Image(systemName: "chevron.up.chevron.down")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .popover(isPresented: $showTree, arrowEdge: .bottom) {
                PresetTreeView(model: model)
            }
            .accessibilityLabel(L("EQ 프리셋", "EQ preset", "EQ プリセット", "Préréglage EQ", "EQ-Preset"))
            .accessibilityValue(model.currentPresetName)
            .frame(maxWidth: .infinity)

            if model.isCustomCurve {
                Divider().frame(height: 18)
                TextField("Custom", text: $saveName)
                    .textFieldStyle(.plain)
                    .textContentType(nil)
                    .autocorrectionDisabled()
                    .font(.system(size: 12))
                    .frame(width: 64)
                    .onSubmit(save)
                Button(action: save) {
                    Image(systemName: "square.and.arrow.down")
                        .font(.system(size: 11, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.blue(scheme))
                        .frame(width: 24, height: 24)
                        .contentShape(Circle())
                }
                .buttonStyle(.plain)
                .help(saveName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
                      ? L("Custom 저장", "Save Custom", "カスタムを保存", "Enregistrer Custom", "Custom speichern")
                      : L("프리셋 저장", "Save preset", "プリセットを保存", "Enregistrer le préréglage", "Preset speichern"))
                .accessibilityLabel(L("EQ 프리셋 저장", "Save EQ preset", "EQ プリセットを保存",
                                      "Enregistrer le préréglage EQ", "EQ-Preset speichern"))
            }
        }
        .padding(.horizontal, 9)
        .padding(.vertical, 8)
        .frame(maxWidth: .infinity)
        // `.clear` (not `.regular`) so it doesn't read as a bright white pill when it
        // sits over the already-frosted sheet glass (glass-on-glass); clip trims the
        // drop shadow so it lies flat.
        .glassEffect(.clear, in: Capsule())
        .clipShape(Capsule())
        .disabled(!model.status.reachable)
        .animation(.snappy(duration: 0.22), value: model.isCustomCurve)
    }

    private func save() {
        model.saveCurrentAsPreset(name: saveName)
        saveName = ""
    }

    private func resetToFlat() {
        if let flat = PresetLibrary.all.first(where: { $0.name == "Flat" }) {
            model.applySavedPreset(flat)
        } else {
            model.apply(presetId: "flat")
        }
    }
}

// MARK: - Preset tree (folder-grouped library + My Presets), Liquid Glass popover

struct PresetTreeView: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    @State private var expanded: Set<String> = ["Signature"]

    private var activeName: String? { model.activeSavedPreset }

    // "Engine" surfaces the engine's own built-in presets (e.g. Dialogue, whose
    // internal compressor the app library can't reproduce); everything else is the
    // app library / user presets.
    private var folderOrder: [String] { ["Engine"] + PresetLibrary.folderOrder }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "slider.horizontal.3").font(.system(size: 12, weight: .semibold))
                Text(L("EQ Presets", "EQ Presets", "EQ プリセット", "Préréglages EQ", "EQ-Presets"))
                    .font(.system(size: 13, weight: .semibold))
                Spacer()
                Text(model.currentPresetName)
                    .font(.system(size: 11, weight: .medium)).foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            .padding(.horizontal, 14).padding(.vertical, 11)

            ScrollView {
                VStack(spacing: 8) {
                    ForEach(folderOrder, id: \.self) { folder in
                        folderSection(folder)
                    }
                }
                .padding(.horizontal, 10).padding(.bottom, 12)
            }
        }
        .frame(width: 320, height: 460)
    }

    private func presets(in folder: String) -> [SavedPreset] {
        folder == "My Presets" ? model.savedPresets : PresetLibrary.all.filter { $0.folder == folder }
    }

    private func count(in folder: String) -> Int {
        folder == "Engine" ? model.presets.count : presets(in: folder).count
    }

    // Display name only — `folder` itself stays the identity key used for grouping.
    // Brand/proper folders (Engine/Signature/Apple) keep their English names.
    private func folderTitle(_ folder: String) -> String {
        switch folder {
        case "Speakers":   return L("Speakers", "Speakers", "スピーカー", "Haut-parleurs", "Lautsprecher")
        case "Headphones": return L("Headphones", "Headphones", "ヘッドフォン", "Casques", "Kopfhörer")
        case "My Presets": return L("My Presets", "My Presets", "マイプリセット", "Mes préréglages", "Meine Presets")
        default:           return folder
        }
    }

    private func folderIcon(_ folder: String) -> String {
        switch folder {
        case "Engine":     return "cpu"
        case "Signature":  return "waveform"
        case "Apple":      return "apple.logo"
        case "Speakers":   return "hifispeaker"
        case "Headphones": return "headphones"
        default:           return "star"
        }
    }

    @ViewBuilder
    private func folderSection(_ folder: String) -> some View {
        let open = expanded.contains(folder)
        VStack(spacing: 0) {
            Button {
                withAnimation(.snappy(duration: 0.2)) {
                    if open { expanded.remove(folder) } else { expanded.insert(folder) }
                }
            } label: {
                HStack(spacing: 9) {
                    Image(systemName: folderIcon(folder))
                        .font(.system(size: 12)).frame(width: 18)
                        .foregroundStyle(RoomcutTokens.blue(scheme))
                    Text(folderTitle(folder)).font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                    Spacer()
                    Text("\(count(in: folder))").font(.system(size: 11))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                    Image(systemName: open ? "chevron.down" : "chevron.right")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                }
                .padding(.horizontal, 12).padding(.vertical, 10)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)

            if open {
                VStack(spacing: 2) {
                    if folder == "Engine" {
                        ForEach(model.presets) { engineRow($0) }
                    } else {
                        ForEach(presets(in: folder)) { presetRow($0, folder: folder) }
                        if presets(in: folder).isEmpty && folder == "My Presets" {
                            Text(L("EQ Preset 아래 저장으로 추가됩니다.", "Added via Save under EQ Preset.",
                                   "EQ プリセットの下の保存で追加されます。", "Ajouté via Enregistrer sous Préréglage EQ.",
                                   "Über Speichern unter EQ-Preset hinzugefügt."))
                                .font(.system(size: 11)).foregroundStyle(RoomcutTokens.textTertiary(scheme))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding(.horizontal, 14).padding(.vertical, 6)
                        }
                    }
                }
                .padding(.bottom, 6)
            }
        }
        .glassEffect(.regular, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
    }

    private func engineRow(_ preset: EnginePreset) -> some View {
        let active = model.presetPickerSelection == preset.id
        return HStack(spacing: 9) {
            Image(systemName: active ? "checkmark.circle.fill" : "circle")
                .font(.system(size: 12))
                .foregroundStyle(active ? RoomcutTokens.blue(scheme) : RoomcutTokens.textTertiary(scheme).opacity(0.5))
                .frame(width: 18)
            Text(preset.name)
                .font(.system(size: 12, weight: active ? .semibold : .regular))
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            Spacer()
        }
        .padding(.horizontal, 14).padding(.vertical, 7)
        .background(active ? RoomcutTokens.blue(scheme).opacity(0.12) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 9, style: .continuous))
        .contentShape(Rectangle())
        .onTapGesture { model.applyPickerSelection(preset.id) }
        .padding(.horizontal, 6)
    }

    private func presetRow(_ preset: SavedPreset, folder: String) -> some View {
        let active = preset.name == activeName
        return HStack(spacing: 9) {
            Image(systemName: active ? "checkmark.circle.fill" : "circle")
                .font(.system(size: 12))
                .foregroundStyle(active ? RoomcutTokens.blue(scheme) : RoomcutTokens.textTertiary(scheme).opacity(0.5))
                .frame(width: 18)
            Text(preset.name)
                .font(.system(size: 12, weight: active ? .semibold : .regular))
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            Spacer()
            if folder == "My Presets" {
                Button { model.deleteSavedPreset(preset) } label: {
                    Image(systemName: "trash").font(.system(size: 10))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 14).padding(.vertical, 7)
        .background(active ? RoomcutTokens.blue(scheme).opacity(0.12) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 9, style: .continuous))
        .contentShape(Rectangle())
        .onTapGesture { model.applySavedPreset(preset) }
        .padding(.horizontal, 6)
    }
}

// MARK: - Circular arc knob (drag to change, click dB to type)

private struct MacroKnob: View {
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
