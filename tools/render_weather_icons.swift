// Convert the pinned SVG sources to oversized PNG intermediates with AppKit.
import AppKit
let root=URL(fileURLWithPath:CommandLine.arguments[1]),output=URL(fileURLWithPath:CommandLine.arguments[2])
try FileManager.default.createDirectory(at:output,withIntermediateDirectories:true)
for source in try FileManager.default.contentsOfDirectory(at:root,includingPropertiesForKeys:nil).filter({$0.pathExtension=="svg"}).sorted(by:{$0.path<$1.path}) {
    guard let img=NSImage(contentsOf:source),let rep=NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:192,pixelsHigh:192,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0) else { fatalError("Cannot render \(source)") }
    NSGraphicsContext.saveGraphicsState();NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:rep)
    img.draw(in:NSRect(x:0,y:0,width:192,height:192));NSGraphicsContext.restoreGraphicsState()
    try rep.representation(using:.png,properties:[:])!.write(to:output.appendingPathComponent(source.deletingPathExtension().lastPathComponent+".png"))
}
