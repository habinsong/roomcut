import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The preset selector shown in the Basic tab.
// MARK: - EQ preset selector (native glass, no accent tint)

struct EqPresetPicker: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    @State private var showTree = false
    @State private var saveName = ""

    var body: some View {
        HStack(spacing: 8) {
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
