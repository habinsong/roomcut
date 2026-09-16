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
        .onDisappear { model.endParameterEdit() }
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
            beginEdit: { model.beginParameterEdit() },
            onChange: { model.setMacro(macro, normalized: $0) },
            onSetDb: {
                model.beginParameterEdit()
                model.setMacro(macro, normalized: $0 / 6.0)
                model.endParameterEdit()
            },
            onEditingEnded: { model.endParameterEdit() }
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
