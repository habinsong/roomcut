import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The top bar — output device picker and the engine ON switch.
// MARK: - TopBar (device selector + ON toggle)

struct TopBar: View {
    @ObservedObject var model: RoomcutViewModel
    // White over a dark wash, black over a bright dark-mode wash, normal tokens
    // otherwise — synced with the tab bar.
    var darkBackdrop: Bool = false
    var brightBackdrop: Bool = false
    @Environment(\.colorScheme) private var scheme
    private let chromeControlLift: CGFloat = 10

    private var ink: Color {
        if darkBackdrop { return .white }
        if brightBackdrop { return .black }
        return RoomcutTokens.textPrimary(scheme)
    }
    // Master switch: ON when Roomcut is the system default output (audio routed
    // through the engine), OFF when it's fully out of the path.
    private var isOn: Bool { model.roomcutIsDefault }
    private var deviceName: String {
        model.outputDevices.first { $0.uid == model.selectedDeviceUID }?.name ?? "출력 장치"
    }
    private var dotColor: Color {
        if !model.status.reachable { return RoomcutTokens.red }
        if !isOn { return RoomcutTokens.amber }
        return RoomcutTokens.green
    }

    var body: some View {
        HStack(spacing: 10) {
            // Device selector — no focus ring, background only while pressed.
            Menu {
                ForEach(model.outputDevices) { d in
                    Button {
                        model.selectDevice(d.uid)
                    } label: {
                        if d.uid == model.selectedDeviceUID {
                            Label(d.name, systemImage: "checkmark")
                        } else { Text(d.name) }
                    }
                }
            } label: {
                HStack(spacing: 6) {
                    Image(systemName: "hifispeaker").font(.system(size: 11, weight: .medium))
                    Text(deviceName)
                        .font(.system(size: 12, weight: .medium))
                        .lineLimit(1).truncationMode(.tail)
                    Image(systemName: "chevron.down").font(.system(size: 8, weight: .semibold))
                }
                .foregroundStyle(ink)
                .padding(.horizontal, 11).padding(.vertical, 6)
            }
            .menuStyle(.button)
            .buttonStyle(PressGlassButtonStyle())
            .menuIndicator(.hidden)
            .fixedSize()
            .focusable(false)
            .disabled(!model.status.reachable)
            .accessibilityLabel("출력 장치, 현재 \(deviceName)")
            .offset(y: -chromeControlLift)

            WindowDragHandle()
                .frame(maxWidth: .infinity)
                .frame(height: 32)

            // ON / OFF — master switch (routes the system default in/out of
            // Roomcut). Background only while pressed.
            Button {
                model.setMasterEnabled(!isOn)
            } label: {
                HStack(spacing: 7) {
                    Circle().fill(dotColor).frame(width: 7, height: 7)
                    Text(isOn ? "ON" : "OFF")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(ink)
                }
                .padding(.horizontal, 12).padding(.vertical, 6)
            }
            .buttonStyle(PressGlassButtonStyle())
            .focusable(false)
            .disabled(!model.status.reachable)
            .accessibilityLabel("Roomcut 전원")
            .accessibilityValue(isOn ? "켜짐" : "꺼짐")
            .offset(y: -chromeControlLift)
        }
        .frame(height: 32)
    }
}

// A button whose glass background appears only while pressed (no resting pill,
// no focus ring) — used for the chrome controls that should read as bare text.
struct PressGlassButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background {
                if configuration.isPressed {
                    Capsule().fill(.regularMaterial)
                }
            }
            .contentShape(Capsule())
            .opacity(configuration.isPressed ? 0.85 : 1)
    }
}
