import AppKit
import SceneKit
import SwiftUI

struct SpatialSceneSettings: Equatable {
    var width, center, crossfeed, damping: Double
    var headphone: Bool
    var surround: Double
    var ambience: Bool
    var room, centerWidth, depth, amount, balance, yaw: Double
    var tracking, dark: Bool

    var upmix: Bool { surround >= 2 }
    var automaticRoom: Bool { headphone && upmix && room < 1 }
    var roomIndex: Int { automaticRoom ? 1 : min(3, max(0, Int(room.rounded()))) }
    var wet: Double { automaticRoom ? 0.5 : unit(amount / 100) * (headphone ? 1 : 0.5) }
    var cross: Double { headphone && (upmix || tracking) ? 0 : unit(crossfeed / 100) }
    var diffuse: Double { upmix ? unit(depth / 100) : (ambience ? 0.45 : 0) }
    var discrete: Double { headphone && upmix ? unit(centerWidth / 100) : 0 }
    var heading: CGFloat { headphone && tracking ? CGFloat(-yaw * .pi / 180) : 0 }
    func gain(_ side: Double) -> Double { 1 - unit(side < 0 ? balance : -balance) }
    func unit(_ value: Double) -> Double { min(1, max(0, value)) }
}

// A real 3D cutaway room. The camera, room and channels share a fixed world;
// only the head rotates. No SCNActions, timers, physics or continuous rendering.
// Parameter geometry is rebuilt only on edits; yaw updates touch one transform.
final class SpatialFieldScene {
    let scene = SCNScene()
    let camera = SCNNode()
    private let content = SCNNode()
    private(set) var head = SCNNode()
    private var current: SpatialSceneSettings?
    private var blue = NSColor.systemBlue
    private var text = NSColor.white
    private var shell = NSColor.gray
    private var floor = NSColor.gray
    private var silver = NSColor.lightGray

    init() {
        camera.camera = SCNCamera()
        camera.camera?.fieldOfView = 36
        camera.camera?.zNear = 0.1
        camera.camera?.zFar = 60
        camera.position = SCNVector3(0, 8.6, 10.2)
        camera.look(at: SCNVector3(0, 0.15, -0.2))
        scene.rootNode.addChildNode(camera)
        scene.rootNode.addChildNode(content)
        let ambient = SCNNode()
        ambient.light = SCNLight()
        ambient.light?.type = .ambient
        ambient.light?.intensity = 350
        scene.rootNode.addChildNode(ambient)
        let key = SCNNode()
        key.light = SCNLight()
        key.light?.type = .directional
        key.light?.intensity = 650
        key.light?.castsShadow = true
        key.light?.shadowColor = NSColor.black.withAlphaComponent(0.18)
        key.light?.shadowRadius = 3
        key.light?.shadowMapSize = CGSize(width: 1024, height: 1024)
        key.eulerAngles = SCNVector3(-0.8, -0.55, 0)
        scene.rootNode.addChildNode(key)
    }

    @discardableResult
    func update(_ s: SpatialSceneSettings) -> Bool {
        guard current != s else { return false }
        SCNTransaction.begin()
        SCNTransaction.disableActions = true
        defer { SCNTransaction.commit() }
        if var old = current {
            old.yaw = s.yaw
            if old == s {
                head.eulerAngles.y = s.heading
                current = s
                return true
            }
        }
        current = s
        let scheme: ColorScheme = s.dark ? .dark : .light
        blue = NSColor(RoomcutTokens.blue(scheme))
        text = NSColor(RoomcutTokens.textPrimary(scheme))
        floor = NSColor(s.dark ? Color(hex: 0x282D35) : Color(hex: 0xE7E9EE))
        shell = NSColor(s.dark ? Color(hex: 0x646D7B) : Color(hex: 0x434C5A))
        silver = NSColor(s.dark ? Color(hex: 0xCDD3DC) : Color(hex: 0xB1BBC9))
        content.childNodes.forEach { $0.removeFromParentNode() }
        makeRoom(s)
        makeSoundField(s)
        makeSpeakers(s)
        makeSurround(s)
        makeListener(s)
        return true
    }

    private func makeRoom(_ s: SpatialSceneSettings) {
        box(9.1, 0.13, 8.0, at: SCNVector3(0, -0.13, 0), color: floor, radius: 0.16)
        // Quiet seams establish depth even with the virtual room switched off.
        for z in [-2.9, 0.0, 2.9] {
            tube(from: SCNVector3(-4.25, -0.052, z), to: SCNVector3(4.25, -0.052, z),
                 radius: 0.009, color: text.withAlphaComponent(0.12))
        }
        let roomWidth = [8.5, 7.2, 8.0, 8.7][s.roomIndex]
        let roomLength = [7.2, 6.3, 7.0, 7.6][s.roomIndex]
        let height = s.roomIndex == 0 ? 0.09 : 0.65 + Double(s.roomIndex) * 0.22
        let wall = floor.blended(withFraction: s.dark ? 0.1 : 0.25, of: .white)!
        box(roomWidth, height, 0.10, at: SCNVector3(0, height / 2, -roomLength / 2), color: wall)
        // Cutaway side walls leave the listener and rear channels unobstructed.
        for side in [-1.0, 1.0] {
            box(0.10, height * 0.42, roomLength,
                at: SCNVector3(side * roomWidth / 2, height * 0.21, 0), color: wall)
            if s.roomIndex > 0 {
                tube(from: SCNVector3(side * roomWidth / 2, height * 0.42 + 0.015, -roomLength / 2),
                     to: SCNVector3(side * roomWidth / 2, height * 0.42 + 0.015, roomLength / 2),
                     radius: 0.022, color: blue.withAlphaComponent(0.12 + s.wet * 0.65))
            }
            // Damping absorbs the original side field, independently of RoomSim.
            for z in [-1.8, -0.8, 0.2] {
                box(0.065, 0.27, 0.55, at: SCNVector3(side * (roomWidth / 2 - 0.09), 0.20, z),
                    color: shell.blended(withFraction: s.unit(s.damping / 200) * 0.8, of: blue)!, radius: 0.03)
            }
        }
        if s.roomIndex > 0 {
            for index in 0..<s.roomIndex {
                let z = roomLength / 2 - 0.12 - Double(index) * 0.15
                tube(from: SCNVector3(-roomWidth / 2 + 0.25, 0.018, z),
                     to: SCNVector3(roomWidth / 2 - 0.25, 0.018, z),
                     radius: 0.013, color: blue.withAlphaComponent(s.wet * 0.5))
            }
        }
        label("FRONT", at: SCNVector3(0, height + 0.65, -roomLength / 2), scale: 0.32, color: text.withAlphaComponent(0.6))
    }

    private func makeSoundField(_ s: SpatialSceneSettings) {
        let spread = 0.65 + s.unit((s.width + 200) / 400) * 2.8 + (s.headphone ? 0 : s.cross * 0.5)
        for side in [-1.0, 1.0] {
            let gain = s.gain(side)
            let path = NSBezierPath()
            path.move(to: NSPoint(x: 0, y: 0.25))
            path.curve(to: NSPoint(x: side * spread, y: 2.8),
                       controlPoint1: NSPoint(x: side * spread * 0.7, y: 0.4),
                       controlPoint2: NSPoint(x: side * spread * 1.1, y: 1.9))
            path.curve(to: NSPoint(x: 0, y: 3.0),
                       controlPoint1: NSPoint(x: side * spread, y: 3.3),
                       controlPoint2: NSPoint(x: side * spread * 0.4, y: 3.0))
            path.close()
            shape(path, height: 0.035, y: 0.045,
                  color: floor.blended(withFraction: gain * 0.32, of: blue)!)
            // Two raised contours give the field volume without animated pulses.
            for layer in 0..<2 {
                let radius = spread + 0.10 + Double(layer) * 0.20
                let alpha = (1 - s.unit(s.damping / 200)) * gain * 0.22
                arc(from: 35, to: 145, radiusX: radius, radiusZ: 1.42,
                    centerZ: -1.52, y: 0.07, side: side, color: blue.withAlphaComponent(alpha), thickness: 0.015)
            }
        }
        if s.center > 0 {
            let amount = s.unit(s.center / 200)
            box(0.54 - amount * 0.36, 0.05 + amount * 0.12, 2.1,
                at: SCNVector3(0, 0.11, -1.62), color: blue.withAlphaComponent(0.2 + amount * 0.5), radius: 0.05)
        }
        if s.cross > 0 {
            for side in [-1.0, 1.0] {
                curve(from: SCNVector3(side * 1.55, 0.52, -2.68),
                      control: SCNVector3(0, 0.85, -1.2), to: SCNVector3(-side * 0.36, 0.76, 0),
                      color: blue.withAlphaComponent(s.cross * 0.65), dashed: !s.headphone)
            }
        }
    }

    private func makeSpeakers(_ s: SpatialSceneSettings) {
        for side in [-1.0, 1.0] {
            let node = speaker(at: SCNVector3(side * 1.55, 0, -2.68), centre: false,
                               strength: s.gain(side), title: side < 0 ? "L" : "R")
            node.eulerAngles.y = -side * .pi / 6
        }
        if s.headphone && s.upmix {
            speaker(at: SCNVector3(0, 0, -2.96), centre: true, strength: s.discrete, title: "C")
        }
        if s.discrete < 1 {
            for x in stride(from: -1.3, through: 1.3, by: 0.22) {
                tube(from: SCNVector3(x, 0.07, -2.72), to: SCNVector3(x + 0.11, 0.07, -2.72),
                     radius: 0.016, color: blue.withAlphaComponent((1 - s.discrete) * 0.6))
            }
        }
    }

    @discardableResult
    private func speaker(at position: SCNVector3, centre: Bool, strength: Double, title: String) -> SCNNode {
        let node = SCNNode()
        node.position = position
        content.addChildNode(node)
        let width = centre ? 0.76 : 0.57
        let height = centre ? 0.43 : 1.03
        let body = box(width, height, 0.46, at: SCNVector3(0, height / 2, 0), color: shell, radius: 0.06, parent: node)
        body.opacity = 0.45 + strength * 0.55
        let driver = SCNCylinder(radius: centre ? 0.12 : 0.17, height: 0.035)
        driver.radialSegmentCount = 32
        let woofer = SCNNode(geometry: driver)
        woofer.geometry?.firstMaterial = material(floor)
        woofer.position = SCNVector3(0, centre ? 0.23 : 0.34, 0.24)
        woofer.eulerAngles.x = .pi / 2
        node.addChildNode(woofer)
        let trim = SCNTorus(ringRadius: centre ? 0.13 : 0.18, pipeRadius: 0.017)
        let ring = SCNNode(geometry: trim)
        ring.geometry?.firstMaterial = material(blue.withAlphaComponent(0.25 + strength * 0.75))
        ring.eulerAngles.x = .pi / 2
        ring.position = woofer.position
        node.addChildNode(ring)
        if !centre {
            sphere(0.07, at: SCNVector3(0, 0.77, 0.23), color: silver, parent: node)
        }
        label(title, at: SCNVector3(position.x, height + 0.30, position.z), scale: 0.52, color: text)
        return node
    }

    private func makeSurround(_ s: SpatialSceneSettings) {
        guard s.ambience || s.upmix else { return }
        let layers = s.headphone && s.surround >= 3 ? 2 : 1
        for side in [-1.0, 1.0] {
            let strength = s.diffuse * s.gain(side)
            for layer in 0..<layers {
                arc(from: s.headphone ? 65 : 35, to: s.headphone ? (layers == 2 ? 158 : 133) : 92,
                    radiusX: 2.2 + Double(layer) * 0.42, radiusZ: 2.2 + Double(layer) * 0.42,
                    centerZ: 0, y: 0.13 + strength * 0.18, side: side,
                    color: blue.withAlphaComponent(strength * 0.8), thickness: 0.025 + strength * 0.055)
            }
            if s.headphone && s.upmix {
                let angles: [Double] = s.surround >= 3 ? [90, 135] : [110]
                for (index, angle) in angles.enumerated() {
                    let p = Self.channelPosition(side * angle)
                    let marker = SCNCylinder(radius: 0.19, height: 0.08 + strength * 0.34)
                    let node = SCNNode(geometry: marker)
                    node.geometry?.firstMaterial = material(blue.withAlphaComponent(0.12 + strength * 0.6))
                    node.position = SCNVector3(p.x, 0.06 + strength * 0.17, p.z)
                    content.addChildNode(node)
                    let ring = SCNNode(geometry: SCNTorus(ringRadius: 0.22, pipeRadius: 0.018))
                    ring.geometry?.firstMaterial = material(blue.withAlphaComponent(0.4 + strength * 0.6))
                    ring.position = SCNVector3(p.x, 0.06, p.z)
                    content.addChildNode(ring)
                    label((side < 0 ? "L" : "R") + (index == 0 ? "s" : "b"),
                          at: SCNVector3(p.x, 0.70, p.z), scale: 0.40, color: text)
                }
            }
        }
    }

    private func makeListener(_ s: SpatialSceneSettings) {
        // Shoulders anchor a recognisable listener. The head and headphone cups
        // rotate together; neither the body nor the world follows the sensor.
        let shoulders = sphere(0.52, at: SCNVector3(0, 0.24, 0.15), color: shell)
        shoulders.scale = SCNVector3(1.18, 0.38, 0.63)
        head = SCNNode()
        head.position = SCNVector3(0, 0.78, 0)
        head.scale = SCNVector3(1.18, 1.18, 1.18)
        head.eulerAngles.y = s.heading
        content.addChildNode(head)
        let skull = sphere(0.33, at: SCNVector3(0, 0, 0), color: silver, parent: head)
        skull.scale = SCNVector3(0.91, 1.12, 1)
        let nose = sphere(0.085, at: SCNVector3(0, -0.02, -0.32), color: silver, parent: head)
        nose.scale = SCNVector3(0.6, 0.75, 1.2)
        if s.headphone {
            for side in [-1.0, 1.0] {
                box(0.10, 0.32, 0.22, at: SCNVector3(side * 0.33, -0.02, 0),
                    color: blue, radius: 0.055, parent: head)
            }
            // A solid, partial 3D headband; no glass material or 2D overlay.
            for index in 0..<20 {
                let a = Double(index) * .pi / 20
                let b = Double(index + 1) * .pi / 20
                tube(from: SCNVector3(cos(a) * 0.35, sin(a) * 0.40, 0.02),
                     to: SCNVector3(cos(b) * 0.35, sin(b) * 0.40, 0.02),
                     radius: 0.034, color: shell, parent: head)
            }
        }
        if s.headphone && s.tracking {
            let tip = SCNVector3(0, -0.55, -0.90)
            tube(from: SCNVector3(0, -0.55, -0.46), to: tip, radius: 0.028, color: blue, parent: head)
            for side in [-1.0, 1.0] {
                tube(from: tip, to: SCNVector3(side * 0.12, -0.55, -0.73), radius: 0.028, color: blue, parent: head)
            }
        }
    }

    static func channelPosition(_ degrees: Double) -> SCNVector3 {
        let a = degrees * .pi / 180
        return SCNVector3(sin(a) * 3.1, 0, -cos(a) * 3.1)
    }

    private func material(_ color: NSColor, constant: Bool = false) -> SCNMaterial {
        let m = SCNMaterial()
        m.diffuse.contents = color.withAlphaComponent(1)
        m.transparency = color.alphaComponent
        m.lightingModel = constant ? .constant : .blinn
        m.specular.contents = NSColor(white: 0.12, alpha: 1)
        m.shininess = 0.35
        if color.alphaComponent < 1 { m.writesToDepthBuffer = false }
        return m
    }

    @discardableResult
    private func box(_ w: Double, _ h: Double, _ d: Double, at p: SCNVector3,
                     color: NSColor, radius: Double = 0.02, parent: SCNNode? = nil) -> SCNNode {
        let shape = SCNBox(width: w, height: h, length: d, chamferRadius: min(radius, min(w, min(h, d)) / 2))
        let node = SCNNode(geometry: shape)
        node.geometry?.firstMaterial = material(color)
        node.position = p
        (parent ?? content).addChildNode(node)
        return node
    }

    @discardableResult
    private func sphere(_ radius: Double, at p: SCNVector3, color: NSColor, parent: SCNNode? = nil) -> SCNNode {
        let ball = SCNSphere(radius: radius)
        ball.segmentCount = 28
        let node = SCNNode(geometry: ball)
        node.geometry?.firstMaterial = material(color)
        node.position = p
        (parent ?? content).addChildNode(node)
        return node
    }

    private func tube(from a: SCNVector3, to b: SCNVector3, radius: Double,
                      color: NSColor, parent: SCNNode? = nil) {
        guard color.alphaComponent > 0.001 else { return }
        let delta = SIMD3<Float>(Float(b.x - a.x), Float(b.y - a.y), Float(b.z - a.z))
        let length = simd_length(delta)
        guard length > 0 else { return }
        let cylinder = SCNCylinder(radius: radius, height: CGFloat(length))
        cylinder.radialSegmentCount = 8
        let node = SCNNode(geometry: cylinder)
        node.geometry?.firstMaterial = material(color, constant: color.alphaComponent < 1)
        node.position = SCNVector3((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2)
        node.simdOrientation = simd_quatf(from: SIMD3<Float>(0, 1, 0), to: delta / length)
        (parent ?? content).addChildNode(node)
    }

    private func shape(_ path: NSBezierPath, height: Double, y: Double, color: NSColor) {
        path.flatness = 0.01
        let node = SCNNode(geometry: SCNShape(path: path, extrusionDepth: height))
        node.geometry?.firstMaterial = material(color, constant: true)
        node.eulerAngles.x = -.pi / 2
        node.position.y = y
        content.addChildNode(node)
    }

    private func arc(from start: Double, to end: Double, radiusX: Double, radiusZ: Double,
                     centerZ: Double, y: Double, side: Double, color: NSColor, thickness: Double) {
        guard color.alphaComponent > 0.001 else { return }
        // One continuous mesh avoids overlapping transparent cylinder end caps.
        var vertices: [SCNVector3] = [], normals: [SCNVector3] = [], indices: [UInt32] = []
        let steps = 48, sides = 8
        for i in 0...steps {
            let a = (start + (end - start) * Double(i) / Double(steps)) * .pi / 180
            for j in 0...sides {
                let b = Double(j) * 2 * .pi / Double(sides)
                let nx = side * sin(a) * cos(b), ny = sin(b), nz = -cos(a) * cos(b)
                vertices.append(SCNVector3(side * sin(a) * radiusX + nx * thickness,
                                           y + ny * thickness, centerZ - cos(a) * radiusZ + nz * thickness))
                normals.append(SCNVector3(nx, ny, nz))
                if i < steps && j < sides {
                    let p = UInt32(i * (sides + 1) + j), q = p + UInt32(sides + 1)
                    indices += side > 0 ? [p, q, p + 1, p + 1, q, q + 1] : [p, p + 1, q, p + 1, q + 1, q]
                }
            }
        }
        let geometry = SCNGeometry(sources: [SCNGeometrySource(vertices: vertices), SCNGeometrySource(normals: normals)],
                                   elements: [SCNGeometryElement(indices: indices, primitiveType: .triangles)])
        geometry.firstMaterial = material(color, constant: true)
        content.addChildNode(SCNNode(geometry: geometry))
    }

    private func curve(from a: SCNVector3, control c: SCNVector3, to b: SCNVector3, color: NSColor, dashed: Bool) {
        func point(_ i: Int) -> SCNVector3 {
            let t = CGFloat(i) / 24, u = 1 - t
            return SCNVector3(u*u*a.x + 2*u*t*c.x + t*t*b.x,
                              u*u*a.y + 2*u*t*c.y + t*t*b.y,
                              u*u*a.z + 2*u*t*c.z + t*t*b.z)
        }
        for i in 0..<24 where !dashed || i % 3 == 0 {
            tube(from: point(i), to: point(i + 1), radius: 0.018, color: color)
        }
    }

    private func label(_ value: String, at p: SCNVector3, scale: Float, color: NSColor) {
        let text = SCNText(string: value, extrusionDepth: 0)
        text.font = NSFont.systemFont(ofSize: 1, weight: .semibold)
        text.flatness = 0.1
        text.firstMaterial = material(color, constant: true)
        let node = SCNNode(geometry: text)
        let bounds = node.boundingBox
        node.pivot = SCNMatrix4MakeTranslation((bounds.max.x + bounds.min.x) / 2, bounds.min.y, 0)
        node.scale = SCNVector3(scale, scale, scale)
        node.position = p
        node.constraints = [SCNBillboardConstraint()]
        node.castsShadow = false
        content.addChildNode(node)
    }
}
