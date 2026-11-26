"""Native radio regression: real codec fixtures and bounded HTTP/HLS playback.
Run on macOS with Command Line Tools and ffmpeg. Neither is a runtime app dependency.
"""
from pathlib import Path
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
import argparse
import subprocess
import tempfile
import threading
import time
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable',type=Path,help='Test an already built app executable instead of compiling the standalone harness')
options=parser.parse_args()
if options.executable and not options.executable.is_file():parser.error('app executable does not exist')
with tempfile.TemporaryDirectory(prefix='vibe-radio-playback-') as temporary:
 base=Path(temporary)
 for name,args in {
  'tone.mp3':['-c:a','libmp3lame','-b:a','96k'],
  'tone.aac':['-c:a','aac','-b:a','64k','-f','adts'],
  'tone.ts':['-c:a','aac','-b:a','64k','-f','mpegts'],
 }.items():
  subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=8','-ac','2',*args,'-y',str(base/name)],check=True)
 # A live connection can begin in an MP3 frame; its payload may resemble
 # one ADTS header. It must not override the actual MPEG frame sequence.
 raw=bytearray((base/'tone.mp3').read_bytes()[4096:])
 raw[1500:1508]=bytes([255,241,76,128,1,31,252,0])
 (base/'midstream.mp3').write_bytes(raw)
 class Handler(BaseHTTPRequestHandler):
  def log_message(self,*args):pass
  def do_GET(self):
   path=self.path.split('?')[0].strip('/');kind='audio/mpeg'
   if path=='slow-headers':
    time.sleep(5)
    try:self.send_error(503)
    except (BrokenPipeError,ConnectionResetError):pass
    return
   if path=='redirect':self.send_response(302);self.send_header('Location','/tone.mp3');self.end_headers();return
   if path=='missing':self.send_error(404);return
   if path=='list.m3u':data=b'#EXTM3U\ntone.mp3?token=retained\n';kind='audio/x-mpegurl'
   elif path=='list.pls':data=b'[playlist]\nFile1=tone.aac\n';kind='audio/x-scpls'
   elif path in ['live.m3u8','stall.m3u8']:
    data=b'#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:100\n#EXTINF:2,\ntone.ts?key=abc\n'
    if path=='live.m3u8':data+=b'#EXT-X-ENDLIST\n'
    kind='application/vnd.apple.mpegurl'
   elif path=='encrypted.m3u8':data=b'#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n#EXTINF:2,\ntone.ts\n';kind='application/vnd.apple.mpegurl'
   elif path=='oversize.m3u8':data=b'#EXTM3U\n'+b'#'*40000;kind='application/vnd.apple.mpegurl'
   elif path=='html':data=b'<html>not audio</html>';kind='text/html'
   elif path=='icy':
    raw=(base/'tone.mp3').read_bytes();data=b''
    for i in range(0,len(raw),1024):
     block=raw[i:i+1024];data+=block
     if len(block)==1024:data+=b'\1'+b'X'*16
   else:
    file=base/path
    if not file.is_file():self.send_error(404);return
    data=file.read_bytes()
   self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)))
   if path=='icy':self.send_header('icy-metaint','1024')
   self.end_headers()
   try:
    for i in range(0,len(data),137):self.wfile.write(data[i:i+137]);self.wfile.flush()
   except (BrokenPipeError,ConnectionResetError):pass
 server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
 server.daemon_threads=True
 threading.Thread(target=server.serve_forever,daemon=True).start()
 main=base/'main.swift'
 main.write_text('import Foundation\nif let i=CommandLine.arguments.firstIndex(of:"--radio-stream-tests") {exit(RadioTests.streaming(base:URL(string:CommandLine.arguments[i+1])!))}\nRadioTests.run { yes,label in if !yes {fputs("FAIL: \\(label)\\n",stderr);exit(1)} }\n\nif let i=CommandLine.arguments.firstIndex(of:"--radio-media") {\n for path in CommandLine.arguments[(i+1)...] where path.hasSuffix(".ts") {\n  let data=try! Data(contentsOf:URL(fileURLWithPath:path));var wholeParser=RadioTransport();let whole=try! wholeParser.audio(data)\n  for size in [1,137,188,4096] {\n   var parser=RadioTransport(),audio=Data()\n   for pos in stride(from:0,to:data.count,by:size) {audio.append(try! parser.audio(data.subdata(in:pos..<min(data.count,pos+size))))}\n   precondition(audio==whole,"TS byte fragments must retain stable Data indices")\n  }\n  print("Stateful MPEG-TS fragmented buffers PASS")\n }\n}\n')
 executable=options.executable.resolve() if options.executable else base/'radio'
 if not options.executable:
  sources=['VoicePolicy.swift','VoiceProtocol.swift','Protocol.swift','VoiceMerge.swift','DeviceDiscovery.swift','TransportPolicy.swift','AppMetadata.swift','RadioLibrary.swift','RadioFormat.swift','RadioPlayback.swift','RadioDelivery.swift','RadioFlowTests.swift','RadioTests.swift']
  subprocess.run(['xcrun','swiftc','-swift-version','5','-whole-module-optimization','-O',*[str(ROOT/'macos/app'/name) for name in sources],str(main),'-o',str(executable)],check=True)
 vector=base/'vector'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'main/vibe'),str(ROOT/'tests/vibe/test_radio_stream.c'),str(ROOT/'main/vibe/adpcm.c'),'-o',str(vector)],check=True)
 subprocess.run([str(vector),str(base/'radio-vector.bin')],check=True)
 try:
  subprocess.run([str(executable),*(['--self-test'] if options.executable else []),'--radio-vector',str(base/'radio-vector.bin'),'--radio-media',*[str(base/name) for name in ['tone.mp3','midstream.mp3','tone.aac','tone.ts']]],check=True,timeout=30)
  subprocess.run([str(executable),'--radio-stream-tests',f'http://127.0.0.1:{server.server_port}/'],check=True,timeout=90)
 finally:
  server.shutdown();server.server_close()
print('Computer-decoded radio native codec/network regression PASS')
