// Native 3D regression checks and review images. No engine or sensor access.
// Build from the repository root:
// DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun swiftc -parse-as-library \
//   apps/macos/Roomcut/SpatialFieldScene.swift apps/macos/Roomcut/RoomcutGlassStyle.swift \
//   scripts/check-spatial-field.swift -o build/check-spatial-field
// Run: build/check-spatial-field build/spatial-field-qa/final
import AppKit
import SceneKit
import SwiftUI

@main struct SpatialFieldCheck {
    static var checks = 0

    static func require(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        guard condition() else { fatalError(message) }
    }

    static func labels(_ scene: SCNScene) -> Set<String> {
        var values = Set<String>()
        scene.rootNode.enumerateChildNodes { node, _ in
            if let text = node.geometry as? SCNText, let value = text.string as? String { values.insert(value) }
        }
        return values
    }

    @MainActor static func main() throws {
        let output = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "build/spatial-field-qa/final",
                         isDirectory: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let base = SpatialSceneSettings(width: 0, center: 0, crossfeed: 0, damping: 0,
            headphone: true, surround: 3, ambience: false, room: 0, centerWidth: 100,
            depth: 50, amount: 50, balance: 0, yaw: 0, tracking: true, dark: true)
        let cases: [(String, (inout SpatialSceneSettings) -> Void)] = [
            ("71", { _ in }),
            ("71-right90", { $0.yaw = 90 }),
            ("71-left90", { $0.yaw = -90 }),
            ("71-back180", { $0.yaw = 180 }),
            ("71-right179", { $0.yaw = 179 }),
            ("71-left179", { $0.yaw = -179 }),
            ("51", { $0.surround = 2 }),
            ("stereo", { $0.surround = 0; $0.tracking = false }),
            ("ambience", { $0.surround = 0; $0.ambience = true; $0.tracking = false }),
            ("speaker-wide", { $0.headphone = false }),
            ("speaker-cross", { $0.headphone = false; $0.surround = 0; $0.crossfeed = 100 }),
            ("71-ignored-cross", { $0.crossfeed = 100 }),
            ("cross", { $0.surround = 0; $0.tracking = false; $0.crossfeed = 100 }),
            ("studio", { $0.room = 1 }),
            ("live", { $0.room = 2 }),
            ("hall", { $0.room = 3 }),
            ("hall0", { $0.room = 3; $0.amount = 0 }),
            ("hall100", { $0.room = 3; $0.amount = 100 }),
            ("center0", { $0.centerWidth = 0 }),
            ("depth0", { $0.depth = 0 }),
            ("depth100", { $0.depth = 100 }),
            ("space-minus200", { $0.width = -200 }),
            ("space-plus200", { $0.width = 200 }),
            ("focus200", { $0.center = 200 }),
            ("damping200", { $0.damping = 200 }),
            ("balance-left", { $0.balance = -1 }),
            ("balance-right", { $0.balance = 1 }),
            ("all-max", { $0.width = 200; $0.center = 200; $0.damping = 200; $0.room = 3;
                $0.amount = 100; $0.depth = 100; $0.yaw = 135; $0.balance = 1 })
        ]
        for dark in [true, false] {
            for (name, change) in cases {
                var state = base
                state.dark = dark
                change(&state)
                let field = SpatialFieldScene()
                require(field.update(state), "Initial state was not applied")
                require(!field.update(state), "Unchanged settings scheduled another render")
                let renderer = SCNRenderer(device: nil, options: nil)
                renderer.scene = field.scene
                renderer.pointOfView = field.camera
                field.scene.background.contents = NSColor(dark ? Color(hex: 0x1C1E22) : .white)
                let size = CGSize(width: 1050, height: 660)
                let image = renderer.snapshot(atTime: 0, with: size, antialiasingMode: .multisampling4X)
                let rep = NSBitmapImageRep(data: image.tiffRepresentation!)!
                try rep.representation(using: .png, properties: [:])!.write(to:
                    output.appendingPathComponent("\(dark ? "dark" : "light")-\(name).png"))

                // The complete floor and tallest wall must remain inside the
                // viewport. This catches the original layout's clipped rears.
                for x in [-4.55, 4.55] {
                    for z in [-4.0, 4.0] {
                        let p = renderer.projectPoint(SCNVector3(x, -0.13, z))
                        require(p.x > 4 && p.x < size.width - 4 && p.y > 4 && p.y < size.height - 4,
                                "Floor clipped: \(name), \(p)")
                    }
                }
                let names = labels(field.scene)
                if state.headphone && state.upmix {
                    require(names.contains("C") && names.contains("Ls") && names.contains("Rs"), "Missing upmix channels")
                    require(names.contains("Lb") == (state.surround >= 3), "Wrong rear channel layout")
                } else {
                    require(names.isDisjoint(with: ["C", "Ls", "Rs", "Lb", "Rb"]), "Phantom output channels on stereo/speakers")
                }
            }
        }

        let field = SpatialFieldScene()
        var state = base
        field.update(state)
        let headID = ObjectIdentifier(field.head)
        let start = CFAbsoluteTimeGetCurrent()
        for degrees in -180...180 {
            state.yaw = Double(degrees)
            field.update(state)
            let forward = field.head.convertPosition(SCNVector3(0, 0, -1), to: field.scene.rootNode)
            let angle = Double(degrees) * .pi / 180
            require(abs(Double(forward.x) - sin(angle) * 1.18) < 0.00001, "Left/right reversed at \(degrees)")
            require(abs(Double(forward.z) + cos(angle) * 1.18) < 0.00001, "Wrong forward axis at \(degrees)")
            require(ObjectIdentifier(field.head) == headID, "Yaw rebuilt geometry")
        }
        let elapsed = CFAbsoluteTimeGetCurrent() - start
        state.yaw = 179; field.update(state)
        let a = field.head.convertPosition(SCNVector3(0, 0, -1), to: nil)
        state.yaw = -179; field.update(state)
        let b = field.head.convertPosition(SCNVector3(0, 0, -1), to: nil)
        require(hypot(a.x - b.x, a.z - b.z) < 0.05, "Wrap crossed the front instead of the rear")
        state.headphone = false; state.yaw = 90; field.update(state)
        require(field.head.eulerAngles.y == 0, "Speaker listener rotated with head tracking")
        state.headphone = true; state.tracking = false; field.update(state)
        require(field.head.eulerAngles.y == 0, "Tracking Off retained a head angle")
        print("\(checks) checks passed; \(cases.count * 2) SceneKit images rendered.")
        print(String(format: "361 yaw updates: %.3f ms (CPU scene updates, not GPU frame time).", elapsed * 1000))
    }
}
