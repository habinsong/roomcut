import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct SoundHistoryControls: View {
    @ObservedObject var model: RoomcutViewModel
    @ObservedObject private var comparison: SoundComparison
    @Environment(\.colorScheme) private var scheme

    init(model: RoomcutViewModel) {
        self.model = model
        self.comparison = model.comparison
    }

    var body: some View {
        HStack(spacing: 8) {
            icon("chevron.left", label: L("실행 취소", "Undo", "取り消す", "Annuler", "Rückgängig"),
                 shortcut: "⌘Z", enabled: model.canUndoSound, action: model.undoSound)
            icon("chevron.right", label: L("다시 실행", "Redo", "やり直す", "Rétablir", "Wiederholen"),
                 shortcut: "⇧⌘Z", enabled: model.canRedoSound, action: model.redoSound)
            Spacer(minLength: 8)
            Button(action: model.toggleLevelMatch) {
                Text(levelMatchTitle)
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(comparison.enabled ? RoomcutTokens.blue(scheme) : RoomcutTokens.textSecondary(scheme))
                    .frame(width: 52, height: 24)
                    .background(comparison.enabled ? RoomcutTokens.blue(scheme).opacity(0.12) : .clear, in: Capsule())
            }
            .buttonStyle(.plain)
            .disabled(!comparison.supported)
            .accessibilityLabel(L("음량 맞춤 비교", "Level-matched comparison", "音量を合わせて比較", "Comparaison à niveau égal", "Pegelabgeglichener Vergleich"))
            .accessibilityValue(levelMatchHelp)
            .accessibilityAddTraits(comparison.enabled ? .isSelected : [])
            .help(levelMatchHelp)
            ForEach(SoundComparisonSlot.allCases, id: \.self) { slot in
                let selected = model.editor.activeSlot == slot
                Button { model.selectComparison(slot) } label: {
                    Text(slot.rawValue)
                        .font(.system(size: 11, weight: .semibold))
                        .foregroundStyle(selected ? RoomcutTokens.blue(scheme) : RoomcutTokens.textSecondary(scheme))
                        .frame(width: 28, height: 24)
                        .background(selected ? RoomcutTokens.blue(scheme).opacity(0.12) : .clear, in: Capsule())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(L("비교 상태", "Comparison", "比較状態", "Comparaison", "Vergleich") + " " + slot.rawValue)
                .accessibilityAddTraits(selected ? .isSelected : [])
            }
            icon("doc.on.doc", label: L("다른 비교 상태에 복사", "Copy to other comparison", "もう一方の比較状態にコピー", "Copier vers l'autre comparaison", "In anderen Vergleich kopieren"),
                 shortcut: model.editor.activeSlot == .a ? "A → B" : "B → A",
                 enabled: model.editor.canCopy, action: model.copyComparison)
        }
        .frame(height: 26)
        .disabled(!model.status.reachable || !model.editor.hasBaseline)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(L("편집 이력과 비교", "Edit history and comparison", "編集履歴と比較", "Historique et comparaison", "Bearbeitungsverlauf und Vergleich"))
    }

    private var levelMatchTitle: String {
        guard comparison.enabled else { return L("레벨", "Level", "レベル", "Niveau", "Pegel") }
        switch comparison.state {
        case .measuring: return L("측정", "…", "測定", "Mesure", "Messen")
        case .noSignal, .bypassed: return L("대기", "Wait", "待機", "Attente", "Warten")
        case .unavailable: return L("오류", "Error", "エラー", "Erreur", "Fehler")
        default: return L("레벨", "Level", "レベル", "Niveau", "Pegel")
        }
    }

    private var levelMatchHelp: String {
        if !comparison.supported {
            return L("엔진을 업데이트하면 사용할 수 있습니다", "Available after updating the engine", "エンジンの更新後に使用できます", "Disponible après mise à jour du moteur", "Nach Aktualisierung der Engine verfügbar")
        }
        switch comparison.state {
        case .disabled:
            return L("A/B를 더 작은 쪽 음량에 맞춰 비교", "Compare A/B at the quieter level", "A/Bを小さい方の音量に合わせて比較", "Comparer A/B au niveau le plus faible", "A/B auf den leiseren Pegel abgleichen")
        case .measuring:
            return L("같은 음원 구간으로 두 설정을 측정하는 중", "Measuring both settings on the same audio", "同じ音源で両方の設定を測定中", "Mesure des deux réglages sur le même audio", "Beide Einstellungen werden am selben Signal gemessen")
        case .matched:
            return L("음량 맞춤 · 현재 설정 감쇠", "Level matched · current reduction", "音量一致・現在の減衰", "Niveau égalisé · atténuation actuelle", "Pegel abgeglichen · aktuelle Absenkung") + String(format: " %.1f dB", comparison.reductionDb)
        case .noSignal:
            return L("비교할 소리가 충분하지 않습니다", "Not enough audio to compare", "比較に十分な音がありません", "Audio insuffisant pour comparer", "Nicht genug Signal für den Vergleich")
        case .bypassed:
            return L("우회 중에는 음량 맞춤을 적용하지 않습니다", "Level matching is inactive during bypass", "バイパス中は音量調整を適用しません", "Égalisation du niveau inactive en bypass", "Pegelabgleich ist im Bypass inaktiv")
        case .unavailable:
            return L("비교 상태를 확인할 수 없습니다", "Comparison state is unavailable", "比較状態を確認できません", "État de comparaison indisponible", "Vergleichsstatus nicht verfügbar")
        }
    }

    private func icon(_ name: String, label: String, shortcut: String,
                      enabled: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: name)
                .font(.system(size: 11, weight: .medium))
                .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                .frame(width: 24, height: 24)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .help(label + " · " + shortcut)
        .accessibilityLabel(label)
    }
}
