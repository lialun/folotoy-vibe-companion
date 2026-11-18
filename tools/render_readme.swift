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

let root = URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true)
let dest = URL(fileURLWithPath:CommandLine.arguments[2],isDirectory:true)
try FileManager.default.createDirectory(at:dest,withIntermediateDirectories:true)
let paper:UInt32=0xEFF6EC, ink:UInt32=0x142B21, green:UInt32=0x237A48, muted:UInt32=0x52685A
func background(_ w:CGFloat,_ h:CGFloat) {
    color(paper).setFill();NSRect(x:0,y:0,width:w,height:h).fill()
    color(0xD5EDCD).setFill();NSBezierPath(ovalIn:NSRect(x:w*0.70,y:-h*0.3,width:w*0.8,height:w*0.8)).fill()
}
func brand(_ x:CGFloat,_ y:CGFloat,_ size:CGFloat) {
    mark(x:x,y:y,size:size)
    text("Vibe 伴侣",x:x+size+22,y:y-2,width:700,size:size*0.80,weight:.semibold,ink:ink)
}
func card(_ rect:NSRect,_ name:String,_ dual:Bool=false) {
    NSGraphicsContext.saveGraphicsState()
    let tr=NSAffineTransform();tr.translateX(by:rect.minX,yBy:rect.minY);tr.scaleX(by:rect.width/1000,yBy:rect.height/660);tr.concat()
    let shadow=NSShadow();shadow.shadowColor=NSColor.black.withAlphaComponent(0.14);shadow.shadowBlurRadius=30;shadow.shadowOffset=NSSize(width:0,height:12);shadow.set()
    rounded(NSRect(x:0,y:0,width:1000,height:660),radius:54,fill:0x14232E)
    NSShadow().set()
    text(name,x:56,y:43,width:888,size:64,weight:.medium,ink:0xF0EBDD)
    if !dual {
        text("每周剩余",x:59,y:155,width:700,size:44,ink:0xB7C9CB)
        text("47%",x:49,y:224,width:890,size:194,weight:.medium,ink:0xF0EBDD)
        rounded(NSRect(x:60,y:503,width:880,height:18),radius:9,fill:0x304953)
        rounded(NSRect(x:60,y:503,width:414,height:18),radius:9,fill:0x51DFC4)
        text("3 天后重置",x:59,y:558,width:880,size:42,ink:0xB7C9CB)
    } else {
        for (i,values) in [("5 小时","74.5%","2 小时 20 分后重置",0.745),("每周","32%","3 天后重置",0.32)].enumerated() {
            let y=CGFloat(160+i*239)
            text(values.0,x:60,y:y,width:400,size:44,ink:0xD6DFD7)
            text(values.1,x:568,y:y-21,width:380,size:86,weight:.medium,ink:0xF0EBDD)
            rounded(NSRect(x:60,y:y+94,width:880,height:15),radius:7,fill:0x304953)
            rounded(NSRect(x:60,y:y+94,width:880*values.3,height:15),radius:7,fill:0x51DFC4)
            text(values.2,x:60,y:y+130,width:880,size:35,ink:0xB7C9CB)
        }
    }
    NSGraphicsContext.restoreGraphicsState()
}
func goCard(_ rect:NSRect) {
    NSGraphicsContext.saveGraphicsState()
    let tr=NSAffineTransform();tr.translateX(by:rect.minX,yBy:rect.minY);tr.scaleX(by:rect.width/1000,yBy:rect.height/660);tr.concat()
    rounded(NSRect(x:0,y:0,width:1000,height:660),radius:54,fill:0x14232E)
    text("OpenCode Go",x:56,y:43,width:888,size:64,weight:.medium,ink:0xF0EBDD)
    for (i,v) in [("5 小时","68%",0.68,"2 小时后重置"),("每周","47%",0.47,"3 天后重置"),("每月","12.3%",0.123,"12 天后重置")].enumerated() {
        let y=CGFloat(148+i*163)
        text(v.0,x:60,y:y,width:420,size:41,ink:0xD6DFD7)
        text(v.1,x:602,y:y-12,width:340,size:67,weight:.medium,ink:0xF0EBDD)
        rounded(NSRect(x:60,y:y+68,width:880,height:12),radius:6,fill:0x304953)
        rounded(NSRect(x:60,y:y+68,width:880*v.2,height:12),radius:6,fill:0x51DFC4)
        text(v.3,x:60,y:y+91,width:880,size:31,ink:0xB7C9CB)
    }
    NSGraphicsContext.restoreGraphicsState()
}
func pill(_ value:String,x:CGFloat,y:CGFloat,width:CGFloat,size:CGFloat=26) {
    rounded(NSRect(x:x,y:y,width:width,height:size*2.0),radius:size,fill:0xD8ECCD)
    text(value,x:x+size*0.8,y:y+size*0.35,width:width-size,size:size,weight:.medium,ink:green)
}
let lcd=try frame(root.appendingPathComponent(".work/readme/frames"),"quota-agent-dark-codex")
guard let product=NSImage(contentsOf:root.appendingPathComponent("docs/brand/ai-passport-front.png")) else{throw ArtworkError.image}
func officialDevice(x:CGFloat,y:CGFloat,width:CGFloat) {
    let s=width/582
    product.draw(in:NSRect(x:x,y:y,width:width,height:938*s),from:.zero,operation:.sourceOver,fraction:1,respectFlipped:true,hints:[.interpolation:NSImageInterpolation.high])
    screen(lcd,in:NSRect(x:x+126*s,y:y+142*s,width:285*s,height:380*s),radius:28*s)
}
try canvas(width:1600,height:900,output:dest.appendingPathComponent("vibe-companion-hero.png"),scale:1) {
    background(1600,900);brand(64,54,58)
    text("语音输入\n额度管理",x:64,y:204,width:720,size:94,weight:.semibold,ink:ink,lineHeight:122)
    text("你的自定义 Vibe 伴侣",x:69,y:483,width:700,size:37,weight:.medium,ink:green)
    text("常用 Agent 额度 · 任务提醒\n番茄钟 · 天气 · 收音机 · 自由接入",x:70,y:566,width:690,size:26,ink:muted,lineHeight:42)
    officialDevice(x:1110,y:162,width:405)
    card(NSRect(x:772,y:235,width:380,height:251),"Codex")
    card(NSRect(x:827,y:562,width:450,height:297),"Claude Code",true)
    pill("配套桌面端 · 蓝牙 / Wi-Fi",x:68,y:728,width:550,size:27)
    text("适配多种使用环境。",x:70,y:807,width:700,size:25,ink:muted)
}
try canvas(width:1200,height:1600,output:dest.appendingPathComponent("vibe-companion-cover.png"),scale:1) {
    background(1200,1600);brand(72,63,82)
    text("语音输入\n额度管理",x:71,y:224,width:1058,size:146,weight:.semibold,ink:ink,lineHeight:172)
    text("你的自定义 Vibe 伴侣",x:78,y:565,width:1050,size:55,weight:.medium,ink:green)
    card(NSRect(x:82,y:660,width:1036,height:684),"Codex")
    rounded(NSRect(x:170,y:1380,width:860,height:104),radius:30,fill:0xD2EBC8)
    symbol("checkmark.circle.fill",x:202,y:1409,width:43,height:43,ink:green)
    text("Claude Code · 任务已完成",x:270,y:1408,width:720,size:40,weight:.medium,ink:ink)
    text("配套桌面端 · 蓝牙 / Wi-Fi",x:82,y:1514,width:1050,size:42,weight:.semibold,ink:green)
}
try canvas(width:1600,height:900,output:dest.appendingPathComponent("vibe-companion-quotas.png"),scale:1) {
    background(1600,900)
    text("常用 Agent 额度，一目了然。",x:70,y:62,width:1450,size:67,weight:.semibold,ink:ink)
    text("Codex · Claude Code · OpenCode Go 等常用 Agent",x:75,y:161,width:1400,size:29,ink:muted)
    card(NSRect(x:76,y:290,width:450,height:297),"Codex")
    card(NSRect(x:574,y:290,width:450,height:297),"Claude Code",true)
    goCard(NSRect(x:1072,y:290,width:450,height:297))
    text("剩余额度 · 重置时间 · 自动更新",x:78,y:791,width:1400,size:34,weight:.medium,ink:green)
}
try canvas(width:1600,height:900,output:dest.appendingPathComponent("vibe-companion-connect.png"),scale:1) {
    background(1600,900)
    text("蓝牙与 Wi-Fi，适配你的使用环境。",x:70,y:65,width:1460,size:64,weight:.semibold,ink:ink)
    let titles=["选蓝牙或 Wi-Fi","电脑点击连接","设备按键确认"]
    let subs=["选择适合自己的方式","找到与你设备相同的尾号","看到电脑名称后确认"]
    for i in 0..<3 {
        let x=CGFloat(74+i*504)
        rounded(NSRect(x:x,y:261,width:445,height:362),radius:36,fill:0xFFFFFF)
        text(String(format:"%02d",i+1),x:x+30,y:291,width:370,size:80,weight:.semibold,ink:0xBADCB0)
        text(titles[i],x:x+31,y:424,width:390,size:41,weight:.semibold,ink:ink)
        text(subs[i],x:x+32,y:512,width:390,size:25,ink:muted)
    }
    text("发现设备 · 发起连接 · 设备确认",x:76,y:704,width:1460,size:38,weight:.medium,ink:green)
    text("语音输入 · 额度管理 · 任务提醒 · 收音机",x:78,y:778,width:1450,size:29,ink:muted)
}
guard let desktopUI=NSImage(contentsOf:root.appendingPathComponent(".work/readme/frames/desktop-ui.png")) else{throw ArtworkError.image}
func desktopPanel(_ rect:NSRect) {
    NSGraphicsContext.saveGraphicsState()
    let shadow=NSShadow();shadow.shadowColor=NSColor.black.withAlphaComponent(0.16);shadow.shadowBlurRadius=34;shadow.shadowOffset=NSSize(width:0,height:15);shadow.set()
    rounded(rect,radius:22,fill:0x10191C)
    NSShadow().set()
    screen(desktopUI,in:rect,radius:22)
    NSGraphicsContext.restoreGraphicsState()
}
try canvas(width:1600,height:1480,output:dest.appendingPathComponent("vibe-companion-desktop.png"),scale:1) {
    background(1600,1480)
    text("配套桌面端，连接与配置更直观。",x:76,y:57,width:1450,size:66,weight:.semibold,ink:ink)
    text("设备连接 · 语音配置 · Agent 接入 · 个性化设置",x:81,y:157,width:1440,size:30,ink:muted)
    desktopPanel(NSRect(x:112,y:238,width:1376,height:1193))
}
try canvas(width:1200,height:1600,output:dest.appendingPathComponent("folotoy-desktop.png"),scale:1) {
    background(1200,1600);brand(72,61,72)
    text("配套桌面端\n连接配置更直观",x:71,y:215,width:1060,size:91,weight:.semibold,ink:ink,lineHeight:119)
    text("设备连接 · 语音配置 · Agent 接入",x:77,y:472,width:1050,size:36,ink:muted)
    desktopPanel(NSRect(x:60,y:548,width:1080,height:936))
    text("设备在手边，设置在电脑上。",x:76,y:1520,width:1050,size:36,weight:.medium,ink:green)
}
func portraitQuota(_ rect:NSRect,_ title:String,_ rows:[(String,String,String,Double)]) {
    rounded(rect,radius:30,fill:0x14232E)
    text(title,x:rect.minX+35,y:rect.minY+24,width:rect.width-70,size:45,weight:.medium,ink:0xF0EBDD)
    let step=(rect.height-105)/CGFloat(rows.count)
    for (i,row) in rows.enumerated() {
        let y=rect.minY+104+CGFloat(i)*step,single=rows.count==1
        text(row.0,x:rect.minX+36,y:y,width:500,size:35,ink:0xB7C9CB)
        text(row.1,x:rect.minX+(single ? 650:760),y:y-(single ? 28:11),width:350,size:single ? 98:54,weight:.medium,ink:0xF0EBDD)
        let barY=y+(single ? 101:52)
        rounded(NSRect(x:rect.minX+36,y:barY,width:rect.width-72,height:10),radius:5,fill:0x304953)
        rounded(NSRect(x:rect.minX+36,y:barY,width:(rect.width-72)*row.3,height:10),radius:5,fill:0x51DFC4)
        text(row.2,x:rect.minX+36,y:barY+19,width:rect.width-72,size:27,ink:0xB7C9CB)
    }
}
try canvas(width:1200,height:1600,output:dest.appendingPathComponent("folotoy-quotas.png"),scale:1) {
    background(1200,1600)
    text("常用 Agent 额度\n一目了然",x:72,y:66,width:1055,size:88,weight:.semibold,ink:ink,lineHeight:112)
    portraitQuota(NSRect(x:72,y:316,width:1056,height:300),"Codex",[("每周剩余","47%","3 天后重置",0.47)])
    portraitQuota(NSRect(x:72,y:646,width:1056,height:340),"Claude Code",[("5 小时","74.5%","2 小时 20 分后重置",0.745),("每周","32%","3 天后重置",0.32)])
    portraitQuota(NSRect(x:72,y:1016,width:1056,height:480),"OpenCode Go",[("5 小时","68%","2 小时后重置",0.68),("每周","47%","3 天后重置",0.47),("每月","12.3%","12 天后重置",0.123)])
    text("剩余额度 · 重置时间 · 自动更新",x:77,y:1544,width:1050,size:34,weight:.medium,ink:green)
}
try canvas(width:1200,height:1600,output:dest.appendingPathComponent("folotoy-connect.png"),scale:1) {
    background(1200,1600);brand(72,61,72)
    text("蓝牙与 Wi-Fi\n适配多种环境",x:72,y:215,width:1055,size:91,weight:.semibold,ink:ink,lineHeight:119)
    let titles=["选择蓝牙或 Wi-Fi","电脑发起连接","设备按键确认"]
    let descriptions=["选择适合自己的连接方式","在 App 中找到你的设备","确认电脑名称，完成连接"]
    for i in 0..<3 {
        let y=CGFloat(520+i*240)
        rounded(NSRect(x:72,y:y,width:1056,height:200),radius:30,fill:0xFFFFFF)
        text(String(format:"%02d",i+1),x:106,y:y+44,width:130,size:72,weight:.semibold,ink:0xA6C99A)
        text(titles[i],x:290,y:y+37,width:790,size:48,weight:.semibold,ink:ink)
        text(descriptions[i],x:292,y:y+117,width:790,size:33,ink:muted)
    }
    text("语音输入 · 额度管理 · 任务提醒",x:77,y:1337,width:1050,size:36,ink:muted)
    text("连接设备，开启你的 Vibe 伴侣。",x:76,y:1437,width:1050,size:43,weight:.medium,ink:green)
}
guard let cover=NSImage(contentsOf:dest.appendingPathComponent("vibe-companion-cover.png")) else{throw ArtworkError.image}
try canvas(width:1200,height:720,output:root.appendingPathComponent(".work/readme/folotoy-thumbnail-review.png"),scale:1) {
    color(0x121212).setFill();NSRect(x:0,y:0,width:1200,height:720).fill()
    text("FoloToy 列表缩略图可读性",x:42,y:32,width:1100,size:31,weight:.semibold)
    text("按 100 / 140 / 236 px 宽度预览；不依赖缩略图里的小字",x:45,y:89,width:1100,size:19,ink:0xA5B5A8)
    for (i,w) in [100.0,140.0,236.0].enumerated() {
        let x=CGFloat(68+i*380),cw:CGFloat=310
        rounded(NSRect(x:x,y:158,width:cw,height:501),radius:24,fill:0x1B1F1D,stroke:0x303C34)
        screen(cover,in:NSRect(x:x+(cw-w)/2,y:188,width:w,height:w*4/3),radius:8)
        text("Vibe 伴侣",x:x+24,y:527,width:cw-48,size:23,weight:.semibold)
        text("语音输入、额度管理，\n你的自定义 Vibe 伴侣。",x:x+25,y:573,width:cw-48,size:18,ink:0xB9C7BC,lineHeight:27)
        text("\(Int(w)) px",x:x+24,y:682,width:cw-48,size:18,ink:0x79D78F)
    }
}
print("Rendered product, desktop and four-image community artwork")
