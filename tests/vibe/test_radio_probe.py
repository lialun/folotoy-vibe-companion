"""Exercise the Mac probe against local live/playlist/error fixtures with real codecs.
Requires macOS command-line tools and ffmpeg; run explicitly on the Mac build host.
"""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import subprocess
import tempfile
import threading
import time

ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='vibe-radio-probe-') as temporary:
    work=Path(temporary)
    for kind,arguments in {'mp3':['-codec:a','libmp3lame','-b:a','64k','-f','mp3'], 'aac':['-codec:a','aac','-b:a','64k','-f','adts'], 'ts':['-codec:a','aac','-b:a','64k','-f','mpegts']}.items():
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=400:sample_rate=44100','-t','2','-ac','1',*arguments,str(work/kind)],check=True)
    media={kind:(work/kind).read_bytes() for kind in ['mp3','aac','ts']}
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args): pass
        def do_GET(self):
            path=self.path.split('?')[0]
            if path=='/redirect':
                self.send_response(302);self.send_header('Location','/mp3');self.end_headers();return
            if path=='/missing': self.send_error(404);return
            if path=='/live':
                self.send_response(200);self.end_headers()
                try:
                    for i in range(20): self.wfile.write(media['mp3'][i*500:(i+1)*500]);self.wfile.flush();time.sleep(.1)
                except (BrokenPipeError,ConnectionResetError): pass
                return
            bodies={
                '/mp3':media['mp3'],'/aac':media['aac'],'/ts':media['ts'],
                '/html':b'<html>not a radio</html>',
                '/query.m3u':b'#EXTM3U\nmp3?token=test\n',
                '/list.pls':b'[playlist]\nFile1=mp3?token=test\nNumberOfEntries=1\n',
                '/master.m3u8':b'#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS="mp4a.40.2"\nmedia.m3u8\n',
                '/media.m3u8':b'#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nts\n#EXT-X-ENDLIST\n',
                '/encrypted.m3u8':b'#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI="key"\n#EXTINF:2,\nts\n',
                '/bad':b'not audio'*5000,
            }
            body=bodies.get(path,b'')
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers()
            try:self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError):pass
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    main=work/'main.swift'
    main.write_text('''import Foundation
let base=CommandLine.arguments[1]
let supported=["mp3","aac","ts","redirect","live","query.m3u","list.pls","master.m3u8"]
for path in supported {
 do { let result=try RadioProbe.inspect(RadioStation(name:"Fixture",url:base+path),deadline:Date().addingTimeInterval(8),depth:0,hls:false)
 if path=="query.m3u" || path=="list.pls" { assert(result.url==base+"mp3?token=test") }
 if path=="master.m3u8" { assert(result.url==base+path) }
 print("PASS \\(path)")
 } catch { fputs("FAIL \\(path): \\(error)\\n",stderr);exit(1) }
}
for path in ["html","missing","encrypted.m3u8","bad"] {
 do { _ = try RadioProbe.inspect(RadioStation(name:"Fixture",url:base+path),deadline:Date().addingTimeInterval(5),depth:0,hls:false);exit(1) }
 catch { print("PASS rejected \\(path)") }
}
''')
    executable=work/'probe'
    subprocess.run(['xcrun','swiftc','-swift-version','5',str(ROOT/'macos/app/AppMetadata.swift'),str(ROOT/'macos/app/RadioLibrary.swift'),str(ROOT/'macos/app/RadioProbe.swift'),str(main),'-o',str(executable)],check=True)
    try:subprocess.run([str(executable),f'http://127.0.0.1:{server.server_port}/'],check=True,timeout=40)
    finally:server.shutdown();server.server_close()
print('Mac custom radio probe: real MP3/AAC/TS, live stream, redirects, relative HLS, M3U/PLS queries, HTML/encryption/error rejection PASS')
