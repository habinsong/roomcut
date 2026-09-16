import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// Two lines of lyrics — the one being sung and the one coming — sized to fit
// whatever space the card can spare.
struct TwoLineLyricView: View {
    let current: String?
    let next: String?
    let fontSize: CGFloat
    let scheme: ColorScheme
    var forceWhite: Bool = false
    var forceBlack: Bool = false

    private var inkBase: Color {
        if forceWhite { return .white }
        if forceBlack { return .black }
        return RoomcutTokens.textPrimary(scheme)
    }

    private var primaryColor: Color {
        inkBase.opacity(0.76)
    }

    private var secondaryColor: Color {
        inkBase.opacity(0.44)
    }

    static func reservedHeight(for fontSize: CGFloat) -> CGFloat {
        slotHeight(for: fontSize) + slotHeight(for: max(11, fontSize - 4)) + 2
    }

    private static func slotHeight(for size: CGFloat) -> CGFloat {
        ceil(size * 2.15 + 6)
    }

    var body: some View {
        VStack(spacing: 2) {
            lyricSlot(
                current,
                size: fontSize,
                weight: .medium,
                color: primaryColor
            )
            lyricSlot(
                next,
                size: max(11, fontSize - 4),
                weight: .regular,
                color: secondaryColor
            )
        }
        .multilineTextAlignment(.center)
        .animation(.easeInOut(duration: 0.18), value: current)
        .animation(.easeInOut(duration: 0.18), value: next)
    }

    private func lyricSlot(
        _ text: String?,
        size: CGFloat,
        weight: Font.Weight,
        color: Color
    ) -> some View {
        ZStack {
            if let text {
                AdaptiveLyricText(
                    text: text,
                    baseSize: size,
                    weight: weight,
                    color: color
                )
                    .id(text)
                    .transition(
                        .asymmetric(
                            insertion: .opacity.combined(with: .move(edge: .bottom)),
                            removal: .opacity.combined(with: .move(edge: .top))
                        )
                    )
            } else {
                Text(" ")
                    .font(.system(size: size, weight: weight))
                    .lineLimit(1)
                    .hidden()
            }
        }
        .frame(maxWidth: .infinity)
        .frame(height: Self.slotHeight(for: size))
        .clipped()
    }
}

struct AdaptiveLyricText: View {
    let text: String
    let baseSize: CGFloat
    let weight: Font.Weight
    let color: Color

    var body: some View {
        GeometryReader { geo in
            let layout = Self.layout(
                text: text,
                width: geo.size.width,
                baseSize: baseSize,
                weight: nsWeight
            )
            Text(layout.text)
                .font(.system(size: layout.fontSize, weight: weight))
                .foregroundStyle(color)
                .lineLimit(layout.lineLimit)
                .minimumScaleFactor(0.92)
                .allowsTightening(true)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
                .animation(.smooth(duration: 0.18), value: layout.text)
                .animation(.smooth(duration: 0.18), value: layout.fontSize)
        }
    }

    private var nsWeight: NSFont.Weight {
        if weight == .ultraLight { return .ultraLight }
        if weight == .thin { return .thin }
        if weight == .light { return .light }
        if weight == .regular { return .regular }
        if weight == .medium { return .medium }
        if weight == .semibold { return .semibold }
        if weight == .bold { return .bold }
        if weight == .heavy { return .heavy }
        if weight == .black { return .black }
        return .regular
    }

    private struct Layout: Equatable {
        let text: String
        let fontSize: CGFloat
        let lineLimit: Int
    }

    private static func layout(
        text: String,
        width: CGFloat,
        baseSize: CGFloat,
        weight: NSFont.Weight
    ) -> Layout {
        let available = max(1, width)
        let singleWidth = measuredWidth(text, size: baseSize, weight: weight)
        let singleScale = min(1, available / max(1, singleWidth))
        if singleScale >= 0.78 {
            return Layout(
                text: text,
                fontSize: max(baseSize * singleScale, baseSize * 0.78),
                lineLimit: 1
            )
        }

        let split = balancedSplit(text, size: baseSize, weight: weight)
        let twoLineText = split.second.isEmpty ? split.first : "\(split.first)\n\(split.second)"
        let firstWidth = measuredWidth(split.first, size: baseSize, weight: weight)
        let secondWidth = measuredWidth(split.second, size: baseSize, weight: weight)
        let twoLineScale = min(1, available / max(1, max(firstWidth, secondWidth)))
        return Layout(
            text: twoLineText,
            fontSize: max(baseSize * twoLineScale, baseSize * 0.58),
            lineLimit: split.second.isEmpty ? 1 : 2
        )
    }

    private static func balancedSplit(
        _ text: String,
        size: CGFloat,
        weight: NSFont.Weight
    ) -> (first: String, second: String) {
        let words = text.split(separator: " ")
        guard words.count > 1 else { return (text, "") }

        var bestFirst = String(words[0])
        var bestSecond = words.dropFirst().joined(separator: " ")
        var bestScore = CGFloat.greatestFiniteMagnitude

        for index in 1..<words.count {
            let first = words[..<index].joined(separator: " ")
            let second = words[index...].joined(separator: " ")
            let firstWidth = measuredWidth(first, size: size, weight: weight)
            let secondWidth = measuredWidth(second, size: size, weight: weight)
            let score = max(firstWidth, secondWidth) + abs(firstWidth - secondWidth) * 0.18
            if score < bestScore {
                bestScore = score
                bestFirst = first
                bestSecond = second
            }
        }

        return (bestFirst, bestSecond)
    }

    private static func measuredWidth(
        _ text: String,
        size: CGFloat,
        weight: NSFont.Weight
    ) -> CGFloat {
        let font = NSFont.systemFont(ofSize: size, weight: weight)
        let attributes: [NSAttributedString.Key: Any] = [.font: font]
        return ceil((text as NSString).size(withAttributes: attributes).width)
    }
}
