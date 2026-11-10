import AppKit
import Foundation
let output=CommandLine.arguments[1]
let set=URL(fileURLWithPath:output).appendingPathComponent("AppIcon.iconset")
try FileManager.default.createDirectory(at:set,withIntermediateDirectories:true)
func rgb(_ r:CGFloat,_ g:CGFloat,_ b:CGFloat)->NSColor { NSColor(srgbRed:r,green:g,blue:b,alpha:1) }
for points in [16,32,128,256,512] {
    for scale in [1,2] {
        let side=points*scale
        let rep=NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:side,pixelsHigh:side,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!
        NSGraphicsContext.saveGraphicsState();NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:rep)
        let factor=CGFloat(side)/1024
        let transform=NSAffineTransform();transform.scale(by:factor);transform.concat()
        let tile=NSBezierPath(roundedRect:NSRect(x:60,y:60,width:904,height:904),xRadius:208,yRadius:208)
        NSGradient(starting:rgb(0.125,0.243,0.231),ending:rgb(0.063,0.137,0.149))!.draw(in:tile,angle:270)
        rgb(0.33,0.44,0.41).withAlphaComponent(0.4).setStroke();tile.lineWidth=2;tile.stroke()
        let mark=NSBezierPath();mark.move(to:NSPoint(x:268,y:640));mark.curve(to:NSPoint(x:485,y:414),controlPoint1:NSPoint(x:268,y:391),controlPoint2:NSPoint(x:432,y:313));mark.curve(to:NSPoint(x:756,y:640),controlPoint1:NSPoint(x:562,y:564),controlPoint2:NSPoint(x:756,y:391));mark.lineWidth=79;mark.lineCapStyle = .round;mark.lineJoinStyle = .round
        rgb(0.69,0.85,0.73).setStroke();mark.stroke()
        rgb(0.86,0.93,0.78).setFill();NSBezierPath(ovalIn:NSRect(x:640,y:681,width:76,height:76)).fill()
        NSGraphicsContext.restoreGraphicsState()
        let name="icon_\(points)x\(points)\(scale==2 ? "@2x" : "").png"
        try rep.representation(using:.png,properties:[:])!.write(to:set.appendingPathComponent(name))
    }
}
