import AppKit
import SwiftUI

// Two companions share one orbit. The same geometry is used at menu-bar size.
enum CompanionBrand {
    static func image(size:CGFloat=18,template:Bool=true)->NSImage {
        let image=NSImage(size:NSSize(width:size,height:size),flipped:false) { rect in
            let scale=rect.width/24
            let transform=AffineTransform(scale:scale)
            let path=NSBezierPath()
            path.move(to:NSPoint(x:3,y:17));path.curve(to:NSPoint(x:11,y:7),controlPoint1:NSPoint(x:3,y:7),controlPoint2:NSPoint(x:8,y:4))
            path.curve(to:NSPoint(x:21,y:17),controlPoint1:NSPoint(x:14,y:10),controlPoint2:NSPoint(x:21,y:7))
            path.lineWidth=3;path.lineCapStyle = .round;path.lineJoinStyle = .round
            path.transform(using:transform);NSColor.labelColor.setStroke();path.stroke()
            let dot=NSBezierPath(ovalIn:NSRect(x:16*scale,y:17*scale,width:3*scale,height:3*scale))
            NSColor.labelColor.setFill();dot.fill();return true
        }
        image.isTemplate=template;return image
    }
}
struct BrandMark:View {
    var size:CGFloat=40
    var body:some View {
        ZStack {
            RoundedRectangle(cornerRadius:size*0.26,style:.continuous).fill(Color(red:0.66,green:0.85,blue:0.69))
            Image(nsImage:CompanionBrand.image(size:size*0.72,template:false)).resizable().frame(width:size*0.72,height:size*0.72).colorMultiply(Color(red:0.06,green:0.15,blue:0.15))
        }.frame(width:size,height:size).accessibilityHidden(true)
    }
}
