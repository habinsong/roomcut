import SwiftUI
import SceneKit
import RoomcutCore

struct SpatialFieldView: View {
    let width: Double
    let center: Double
    let crossfeed: Double
    let room: Double
    let headphone: Bool
    let surroundType: Double
    let ambience: Bool
    let roomType: Double
    let centerWidth: Double
    let surroundDepth: Double
    let roomAmount: Double
    let balance: Double
    let headYaw: Double
    let tracking: Bool
    let accent: Color
    @Environment(\.colorScheme) private var scheme

    private var settings: SpatialSceneSettings {
        SpatialSceneSettings(width: width, center: center, crossfeed: crossfeed,
            damping: room, headphone: headphone, surround: surroundType,
            ambience: ambience, room: roomType, centerWidth: centerWidth,
            depth: surroundDepth, amount: roomAmount, balance: balance,
            yaw: headYaw, tracking: tracking, dark: scheme == .dark)
    }

    private var layoutName: String {
        if surroundType >= 2 { return headphone ? (surroundType >= 3 ? "7.1" : "5.1") : "Wide" }
        return ambience ? "Ambience" : L("스테레오", "Stereo", "ステレオ", "Stéréo", "Stereo")
    }

    private var roomName: String {
        if settings.automaticRoom { return L("기본 룸", "Base room", "基本ルーム", "Salle de base", "Basisraum") }
        return [L("룸 꺼짐", "Room off", "ルーム オフ", "Salle désactivée", "Raum aus"),
                "studio", "live", "hall"][settings.roomIndex]
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 6) {
                Image(systemName: headphone ? "headphones" : "hifispeaker.2")
                    .foregroundStyle(.secondary)
                Text(headphone ? "Headphone" : "Speaker")
                Text("·").foregroundStyle(.secondary)
                Text(layoutName).foregroundStyle(accent)
                Spacer(minLength: 0)
            }
            .font(.system(size: 11, weight: .medium))
            .padding(.leading, 6).padding(.trailing, 34)
            .frame(height: 24)

            // Room, balance and head angle already read from the scene itself
            // (and from the controls below), so the picture carries no captions.
            SpatialSceneView(settings: settings)
                .frame(height: 196)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
        }
        .transaction { $0.animation = nil }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(L("청취 공간 설정", "Listening space settings", "リスニング空間設定",
                              "Réglages de l’espace d’écoute", "Hörraum-Einstellungen"))
        .accessibilityValue(accessibilitySummary)
        .help(L("설정에 따른 3D 청취 공간입니다. 방과 스피커는 고정되고 머리만 실제 방향으로 회전합니다.",
                "3D settings preview. The room and speakers stay fixed while the head turns.",
                "設定に応じた3D空間です。部屋とスピーカーは固定され、頭だけが回転します。",
                "Aperçu 3D des réglages. La pièce et les enceintes restent fixes ; seule la tête tourne.",
                "3D-Einstellungsvorschau. Raum und Lautsprecher bleiben fest; nur der Kopf dreht sich."))
    }

    private var accessibilitySummary: String {
        var values = [headphone ? "Headphone" : "Speaker", layoutName, roomName,
                      "Space \(Int(width / 2)), Center \(Int(center / 2)), Damping \(Int(room / 2))",
                      "Balance \(Int(balance * 100))"]
        if settings.cross > 0 { values.append("\(headphone ? "Crossfeed" : "Crosstalk 3D") \(Int(crossfeed))") }
        if settings.roomIndex > 0 { values.append("Room Amount \(settings.automaticRoom ? 50 : Int(roomAmount))") }
        if headphone && settings.upmix { values.append("Center Width \(Int(centerWidth))") }
        if settings.upmix { values.append("Surround Depth \(Int(surroundDepth))") }
        if headphone && tracking { values.append("Head Tracking \(Int(headYaw))°") }
        return values.joined(separator: ", ")
    }
}

private struct SpatialSceneView: NSViewRepresentable {
    let settings: SpatialSceneSettings

    func makeCoordinator() -> SpatialFieldScene { SpatialFieldScene() }

    func makeNSView(context: Context) -> SCNView {
        let view = SCNView()
        view.scene = context.coordinator.scene
        view.pointOfView = context.coordinator.camera
        view.backgroundColor = .clear
        view.antialiasingMode = .multisampling4X
        view.rendersContinuously = false
        view.isPlaying = false
        view.allowsCameraControl = false
        view.setAccessibilityElement(false)
        return view
    }

    func updateNSView(_ view: SCNView, context: Context) {
        if context.coordinator.update(settings) { view.needsDisplay = true }
    }
}
