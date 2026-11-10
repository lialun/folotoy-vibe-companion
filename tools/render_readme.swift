// Documentation artwork only. Marketing illustrations follow the production UI;
// retain the unaltered firmware frames separately. Nothing is embedded in firmware.
import AppKit

enum ArtworkError: Error { case arguments, invalidFrame(String), image, png }

func color(_ hex: UInt32) -> NSColor {
    NSColor(srgbRed: CGFloat((hex >> 16) & 255) / 255,
            green: CGFloat((hex >> 8) & 255) / 255,
            blue: CGFloat(hex & 255) / 255, alpha: 1)
}

func rounded(_ rect: NSRect, radius: CGFloat, fill: UInt32, stroke: UInt32? = nil) {
    let path = NSBezierPath(roundedRect: rect, xRadius: radius, yRadius: radius)
    color(fill).setFill(); path.fill()
    if let stroke = stroke { color(stroke).setStroke(); path.lineWidth = 1; path.stroke() }
}

func text(_ value: String, x: CGFloat, y: CGFloat, width: CGFloat,
          size: CGFloat, weight: NSFont.Weight = .regular, ink: UInt32 = 0xe9f0e8,
          spacing: CGFloat = 0, lineHeight: CGFloat? = nil) {
    let paragraph = NSMutableParagraphStyle()
    if let height = lineHeight { paragraph.minimumLineHeight = height; paragraph.maximumLineHeight = height }
    (value as NSString).draw(in: NSRect(x: x, y: y, width: width, height: 400), withAttributes: [
        .font: NSFont.systemFont(ofSize: size, weight: weight), .foregroundColor: color(ink),
        .paragraphStyle: paragraph, .kern: spacing
    ])
}

func mark(x: CGFloat, y: CGFloat, size: CGFloat) {
    rounded(NSRect(x: x, y: y, width: size, height: size), radius: size * 0.25, fill: 0x294c47)
    let path = NSBezierPath()
    path.move(to: NSPoint(x: x + size * 0.25, y: y + size * 0.38))
    path.curve(to: NSPoint(x: x + size * 0.48, y: y + size * 0.60),
               controlPoint1: NSPoint(x: x + size * 0.25, y: y + size * 0.66),
               controlPoint2: NSPoint(x: x + size * 0.42, y: y + size * 0.76))
    path.curve(to: NSPoint(x: x + size * 0.74, y: y + size * 0.38),
               controlPoint1: NSPoint(x: x + size * 0.57, y: y + size * 0.45),
               controlPoint2: NSPoint(x: x + size * 0.74, y: y + size * 0.67))
    path.lineWidth = size * 0.077; path.lineCapStyle = .round
    color(0xc6e5c3).setStroke(); path.stroke()
    color(0xc6e5c3).setFill()
    NSBezierPath(ovalIn: NSRect(x: x + size * 0.64, y: y + size * 0.27,
                              width: size * 0.075, height: size * 0.075)).fill()
}

func frame(_ directory: URL, _ name: String) throws -> NSImage {
    let data = try Data(contentsOf: directory.appendingPathComponent(name + ".ppm"))
    let header = Data("P6\n240 320\n255\n".utf8)
    guard data.starts(with: header), data.count == header.count + 240 * 320 * 3,
          let provider = CGDataProvider(data: data.dropFirst(header.count) as CFData),
          let image = CGImage(width: 240, height: 320, bitsPerComponent: 8, bitsPerPixel: 24,
                              bytesPerRow: 720, space: CGColorSpaceCreateDeviceRGB(),
                              bitmapInfo: [], provider: provider, decode: nil,
                              shouldInterpolate: false, intent: .defaultIntent) else {
        throw ArtworkError.invalidFrame(name)
    }
    return NSImage(cgImage: image, size: NSSize(width: 240, height: 320))
}

func screen(_ image: NSImage, in rect: NSRect, radius: CGFloat = 13,
            interpolation: NSImageInterpolation = .high) {
    NSGraphicsContext.saveGraphicsState()
    NSBezierPath(roundedRect: rect, xRadius: radius, yRadius: radius).addClip()
    image.draw(in: rect, from: .zero, operation: .sourceOver, fraction: 1,
               respectFlipped: true, hints: [.interpolation: interpolation])
    NSGraphicsContext.restoreGraphicsState()
}

func device(_ image: NSImage, x: CGFloat, y: CGFloat, width: CGFloat) {
    let scale = width / 296, height = 492 * scale
    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow(); shadow.shadowColor = NSColor.black.withAlphaComponent(0.45)
    shadow.shadowBlurRadius = 32; shadow.shadowOffset = NSSize(width: 0, height: -16); shadow.set()
    rounded(NSRect(x: x, y: y, width: width, height: height), radius: 31 * scale,
            fill: 0x263632, stroke: 0x51645b)
    NSGraphicsContext.restoreGraphicsState()
    rounded(NSRect(x: x + 2, y: y + 2, width: width - 4, height: height - 4),
            radius: 29 * scale, fill: 0x1b2927, stroke: 0x344740)
    rounded(NSRect(x: x + 110 * scale, y: y + 14 * scale,
                   width: 76 * scale, height: 11 * scale), radius: 5 * scale, fill: 0x0d1718)
    screen(image, in: NSRect(x: x + 20 * scale, y: y + 49 * scale,
                            width: 240 * scale, height: 320 * scale), radius: 17 * scale)
    for offset in [86.0, 196.0, 329.0] {
        rounded(NSRect(x: x + 277 * scale, y: y + offset * scale,
                       width: 6 * scale, height: 8 * scale), radius: 3 * scale, fill: 0xadb8aa)
    }
    text("FOLOTOY", x: x + 22 * scale, y: y + 413 * scale,
         width: width - 40, size: 13 * scale, weight: .semibold, ink: 0xb8c7b9, spacing: 2 * scale)
    text("AI PASSPORT", x: x + 22 * scale, y: y + 439 * scale,
         width: width - 40, size: 9 * scale, ink: 0x778c81, spacing: 1.5 * scale)
}

func raster(width: Int, height: Int, scale: Int, draw: () throws -> Void) throws -> NSBitmapImageRep {
    guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width * scale, pixelsHigh: height * scale,
                                       bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                       isPlanar: false, colorSpaceName: .deviceRGB,
                                       bytesPerRow: width * scale * 4, bitsPerPixel: 32),
          let context = NSGraphicsContext(bitmapImageRep: bitmap) else { throw ArtworkError.image }
    NSGraphicsContext.saveGraphicsState()
    defer { NSGraphicsContext.restoreGraphicsState() }
    let cg = context.cgContext
    cg.translateBy(x: 0, y: CGFloat(height * scale)); cg.scaleBy(x: CGFloat(scale), y: -CGFloat(scale))
    NSGraphicsContext.current = NSGraphicsContext(cgContext: cg, flipped: true)
    try draw()
    return bitmap
}

func canvas(width: Int, height: Int, output: URL, scale: Int = 2, draw: () throws -> Void) throws {
    let bitmap = try raster(width: width, height: height, scale: scale, draw: draw)
    guard let png = bitmap.representation(using: .png, properties: [:]) else { throw ArtworkError.png }
    try png.write(to: output)
}

func symbol(_ name: String, x: CGFloat, y: CGFloat, width: CGFloat, height: CGFloat, ink: UInt32) {
    guard let image = NSImage(systemSymbolName: name, accessibilityDescription: nil)?
        .withSymbolConfiguration(NSImage.SymbolConfiguration(paletteColors: [color(ink)])) else { return }
    image.draw(in: NSRect(x: x, y: y, width: width, height: height), from: .zero,
               operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
}

// High-resolution illustrations of the existing weather/focus/radio/notice
// states. Native fonts and vector marks avoid upscaling the LCD's bitmap glyphs.
// These are explicitly labelled illustrations, not firmware screenshots.
func illustratedFrame(_ kind: String, light: Bool = false) throws -> NSImage {
    let bg: UInt32 = light ? 0xC6C1AD : 0x14232E
    let ink: UInt32 = light ? 0x1B2529 : 0xF0EBDD
    let muted: UInt32 = light ? 0x3F4B50 : 0xA8BBC2
    let accent: UInt32 = light ? 0x00765D : 0x51DFC4
    let warm: UInt32 = light ? 0x936100 : 0xF2CB73
    let focus: UInt32 = 0xFFB47D
    let enter: UInt32 = light ? 0x0965AD : 0x8CC9FA
    let exit: UInt32 = light ? 0xB54716 : focus
    let bitmap = try raster(width: 240, height: 320, scale: 4) {
        color(bg).setFill(); NSRect(x: 0, y: 0, width: 240, height: 320).fill()
        func label(_ value: String, _ x: CGFloat, _ y: CGFloat, _ size: CGFloat,
                   _ tint: UInt32, width: CGFloat = 210, weight: NSFont.Weight = .regular) {
            text(value, x: x, y: y, width: width, size: size, weight: weight, ink: tint)
        }
        for i in 0..<3 {
            let height = CGFloat(4 + i * 3)
            rounded(NSRect(x: 18 + CGFloat(i * 5), y: 23 - height, width: 3, height: height), radius: 1, fill: accent)
        }
        label("11/08 16:08", 39, 6, 14, ink, width: 108)
        label("72%", 176, 6, 14, ink, width: 31)
        symbol("battery.75percent", x: 208, y: 10, width: 16, height: 9, ink: ink)
        for (i, h) in [10.0, 18, 26, 18, 10].enumerated() {
            rounded(NSRect(x: 13 + CGFloat(i) * 4, y: 53 - h / 2, width: 2.5, height: h), radius: 1.25, fill: accent)
        }
        label("语音输入就绪", 51, 40, 19, accent, width: 165)
        rounded(NSRect(x: 223, y: 24, width: 10, height: 1.5), radius: 0.7, fill: accent)
        label("语", 222, 30, 14, accent, width: 16)
        label("音", 222, 45, 14, accent, width: 16)
        rounded(NSRect(x: 227, y: 129, width: 2, height: 2), radius: 0, fill: enter)
        label("回", 222, 135, 14, enter, width: 16)
        label("车", 222, 150, 14, enter, width: 16)
        rounded(NSRect(x: 223, y: 175, width: 10, height: 1.5), radius: 0.7, fill: exit)
        symbol(kind == "weather" || kind == "alert" ? "stopwatch" : "rectangle.portrait.and.arrow.right",
               x: 222, y: 182, width: 14, height: 16, ink: exit)
        rounded(NSRect(x: 223, y: 237, width: 10, height: 1.5), radius: 0.7, fill: muted)
        symbol("gearshape.fill", x: 222, y: 244, width: 14, height: 14, ink: muted)
        rounded(NSRect(x: 227, y: 266, width: 2, height: 2), radius: 0, fill: muted)
        let action = kind == "focus" ? ["暂", "停"] : kind == "radio" ? ["换", "台"] : ["翻", "页"]
        label(action[0], 222, 273, 14, kind == "focus" || kind == "radio" ? warm : muted, width: 16)
        label(action[1], 222, 288, 14, kind == "focus" || kind == "radio" ? warm : muted, width: 16)
        if kind == "focus" {
            label("正在专注 · 第1轮", 8, 84, 19, focus)
            label("24:32", 8, 111, 48, focus, weight: .light)
            rounded(NSRect(x: 8, y: 186, width: 204, height: 2), radius: 0, fill: 0x405867)
            rounded(NSRect(x: 8, y: 186, width: 3, height: 2), radius: 0, fill: focus)
            label("专注 25 分钟 → 休息", 8, 204, 16, muted)
            label("已完成 0 轮", 8, 272, 16, muted)
        } else if kind == "radio" {
            label("收音机", 12, 84, 20, ink)
            label("01 / 06", 158, 90, 15, muted, width: 60)
            for i in 0..<25 {
                rounded(NSRect(x: 12 + CGFloat(i) * 8, y: i == 12 ? 120 : 129,
                               width: 1, height: i == 12 ? 22 : (i % 2 == 0 ? 7 : 4)),
                        radius: 0, fill: i == 12 ? warm : 0x405867)
            }
            label("怀旧音乐", 72, 163, 20, ink, width: 137)
            label("正在播放", 77, 216, 16, warm, width: 125)
            label("下一台", 12, 250, 16, muted)
            label("清晨音乐", 12, 274, 16, ink)
        } else {
            label("北京 · 多云", 8, 84, 20, ink, width: 165)
            label("1/4", 185, 90, 14, muted, width: 31)
            label("16°", 8, 112, 36, light ? ink : warm, width: 110)
            symbol("cloud.sun", x: 164, y: 109, width: 43, height: 43, ink: warm)
            label("9° / 18°", 8, 165, 20, ink, width: 115)
            label("雨 20%", 151, 170, 14, muted, width: 65)
            label("体感", 8, 202, 16, muted, width: 100)
            label("湿度", 113, 202, 16, muted, width: 100)
            label("14°", 8, 225, 20, ink, width: 100)
            label("62%", 113, 225, 20, ink, width: 100)
            label("UV 今日最高", 8, 269, 14, muted, width: 102)
            label("5.0 · 中等", 115, 266, 18, warm, width: 102)
            label("0 分钟前更新", 8, 293, 14, muted)
            if kind == "alert" {
                rounded(NSRect(x: 8, y: 118, width: 204, height: 80), radius: 12, fill: 0x263E48, stroke: 0x435B65)
                rounded(NSRect(x: 21, y: 142, width: 32, height: 32), radius: 10, fill: 0x304C61)
                symbol("keyboard", x: 25, y: 150, width: 24, height: 16, ink: 0x9BC6EB)
                label("OpenCode", 64, 128, 16, 0xB4C3C5, width: 134)
                label("等待回答", 64, 153, 20, ink, width: 138)
            }
        }
    }
    guard let cg = bitmap.cgImage else { throw ArtworkError.image }
    return NSImage(cgImage: cg, size: NSSize(width: 240, height: 320))
}

guard CommandLine.arguments.count == 3 else { throw ArtworkError.arguments }
let frames = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
let output = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
let weather = try illustratedFrame("weather", light: true)
let focus = try illustratedFrame("focus")
let radio = try illustratedFrame("radio")
let alert = try illustratedFrame("alert")

try canvas(width: 1600, height: 880, output: output.appendingPathComponent("vibe-companion-hero.png")) {
    let bounds = NSRect(x: 0, y: 0, width: 1600, height: 880)
    NSGradient(starting: color(0x0b1d20), ending: color(0x142f2c))!.draw(in: bounds, angle: 0)
    let orbit = NSBezierPath(ovalIn: NSRect(x: 805, y: 68, width: 690, height: 690))
    orbit.lineWidth = 1; color(0x28473d).setStroke(); orbit.stroke()
    let outer = NSBezierPath(ovalIn: NSRect(x: 710, y: -27, width: 880, height: 880))
    outer.lineWidth = 1; color(0x203c36).setStroke(); outer.stroke()
    mark(x: 84, y: 73, size: 53)
    text("FOLOTOY", x: 157, y: 78, width: 420, size: 15, weight: .medium, ink: 0x9cb9ab, spacing: 3)
    text("Vibe 伴侣", x: 157, y: 101, width: 470, size: 25, weight: .semibold)
    text("随手说话。\n安心专注。\n及时回应 AI。", x: 78, y: 212, width: 710,
         size: 74, weight: .semibold, ink: 0xf0f2e7, lineHeight: 87)
    text("语音输入，随手可用。\nAI 需要你时，即刻提醒。", x: 84, y: 520, width: 650,
         size: 25, ink: 0xb0c5ba, lineHeight: 37)
    device(weather, x: 782, y: 294, width: 270)
    device(alert, x: 1120, y: 135, width: 340)
    text("语音输入  /  Agent 提醒  /  番茄钟  /  天气  /  收音机",
         x: 84, y: 791, width: 1430, size: 16, weight: .medium, ink: 0x91ad9d, spacing: 1)
}

try canvas(width: 1600, height: 600, output: output.appendingPathComponent("vibe-companion-screens.png")) {
    color(0x101f22).setFill(); NSRect(x: 0, y: 0, width: 1600, height: 600).fill()
    let images = [weather, focus, radio, alert]
    let labels = ["天气", "番茄钟", "收音机", "Agent 提醒"]
    let notes = ["四页天气，按需翻看", "专注休息，自主开始", "自选电台，随心排序", "需要你时，亮屏提醒"]
    for index in 0..<4 {
        let x = CGFloat(index) * 388 + 68
        text(String(format: "%02d", index + 1), x: x, y: 34, width: 65, size: 12,
             weight: .medium, ink: 0x78998a, spacing: 2)
        screen(images[index], in: NSRect(x: x, y: 79, width: 300, height: 400), radius: 19)
        text(labels[index], x: x, y: 502, width: 310, size: 23, weight: .semibold)
        text(notes[index], x: x, y: 540, width: 322, size: 15, ink: 0xa0b8aa)
    }
}
// Portrait community cover: UI artwork without a physical-device mockup.
try canvas(width: 576, height: 768, output: output.appendingPathComponent("vibe-companion-cover.png")) {
    let bounds = NSRect(x: 0, y: 0, width: 576, height: 768)
    NSGradient(starting: color(0x0b1d20), ending: color(0x1b3d35))!.draw(in: bounds, angle: -35)
    mark(x: 38, y: 35, size: 44)
    text("Vibe 伴侣", x: 96, y: 36, width: 420, size: 27, weight: .semibold)
    text("随手说话，及时回应 AI", x: 98, y: 75, width: 430, size: 13, ink: 0xa9c8b5)
    text("开口即输入。\nAI 有事会叫你。", x: 36, y: 137, width: 510,
         size: 42, weight: .semibold, lineHeight: 54)
    text("无线语音  ·  Agent 提醒  ·  专注计时", x: 39, y: 264,
         width: 510, size: 16, ink: 0xb3cdbd)
    screen(weather, in: NSRect(x: 36, y: 348, width: 225, height: 300), radius: 18)
    screen(alert, in: NSRect(x: 285, y: 318, width: 252, height: 336), radius: 19)
    text("把常用功能，留在手边。", x: 39, y: 685,
         width: 500, size: 25, weight: .medium)
    text("语音  /  天气  /  番茄钟  /  收音机", x: 40, y: 729,
         width: 430, size: 13, ink: 0x9dbbad)
    text("界面插画", x: 478, y: 731, width: 85, size: 10, ink: 0x7d9d8d)
}

try canvas(width: 1600, height: 600,
           output: output.appendingPathComponent("vibe-companion-screens-actual.png"), scale: 1) {
    color(0x101f22).setFill(); NSRect(x: 0, y: 0, width: 1600, height: 600).fill()
    let names = ["light-home", "focus", "dark-radio-2", "dark-agent-alert"]
    let labels = ["天气", "番茄钟", "收音机", "Agent 提醒"]
    for index in 0..<4 {
        let x = CGFloat(index) * 388 + 68
        text("真实固件渲染", x: x, y: 32, width: 300, size: 14, ink: 0x78998a)
        screen(try frame(frames, names[index]), in: NSRect(x: x, y: 79, width: 300, height: 400),
               radius: 19, interpolation: .none)
        text(labels[index], x: x, y: 502, width: 310, size: 23, weight: .semibold)
        text("240 × 320 · 示例数据", x: x, y: 540, width: 322, size: 15, ink: 0xa0b8aa)
    }
}
print("README artwork: rendered high-resolution illustrations and actual firmware references to \(output.path)")
