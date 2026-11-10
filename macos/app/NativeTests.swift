import Foundation

enum NativeTests {
    static func run() {
        if CommandLine.arguments.contains("--agent-tests") { AgentTests.run();return }
        func check(_ yes:Bool,_ description:String) { if !yes { fputs("FAIL: \(description)\n",stderr);exit(1) } }
        LANTests.run(check)
        check(VoiceCodec.decode(Data(repeating:0,count:803))==nil,"short ADPCM block")
        var block=Data(repeating:0,count:804);block[2]=89
        check(VoiceCodec.decode(block)==nil,"invalid ADPCM step index")
        block[2]=0;check(VoiceCodec.decode(block)==[Int16](repeating:0,count:1600),"silence decode")
        check(Packet.parse(Data([1,0,0x80]))==nil,"truncated UDP audio")
        if case .ping?=Packet.parse(Data([4])) {} else { check(false,"ping") }
        check(Packet.parse(Data([4,1]))==nil,"oversized ping")
        var audio=Data([1,7,0x80]);audio.append(block)
        if case .audio(let sequence,let samples)?=Packet.parse(audio) { check(sequence==7 && samples.count==1600,"wire audio framing") } else { check(false,"wire audio parse") }
        check((try? Shortcut.resolve("ctrl+option+v"))==[59,58,9],"left modifiers")
        check(Shortcut.modifiers[59]==0x40001 && Shortcut.modifiers[58]==0x80020,"WeType side flags")
        check((try? Shortcut.resolve("v+ctrl"))==nil,"ordinary key cannot precede modifier")
        check((try? Shortcut.resolve("ctrl+ctrl+v"))==nil,"duplicate keys")
        check((try? Shortcut.resolve("ctrl++v"))==nil,"empty key")
        check(Shortcut.hasElevatedConflictRisk("right-shift"),"single modifier conflict warning")
        check(Shortcut.hasElevatedConflictRisk("command+v"),"common editing shortcut warning")
        check(!Shortcut.hasElevatedConflictRisk("ctrl+option+v"),"tested multi-modifier shortcut retained")
        let search=RadioDirectory.request(name:"音乐",chineseOnly:true)
        let fields=Dictionary(uniqueKeysWithValues:URLComponents(url:search.url!,resolvingAgainstBaseURL:false)!.queryItems!.map { ($0.name,$0.value ?? "") })
        check(fields==["limit":"10","name":"音乐","language":"chinese","hidebroken":"true","order":"votes","reverse":"true"],"exact requested search parameters")
        let all=Dictionary(uniqueKeysWithValues:URLComponents(url:RadioDirectory.request(name:"",chineseOnly:false).url!,resolvingAgainstBaseURL:false)!.queryItems!.map { ($0.name,$0.value ?? "") })
        check(all["name"]=="" && all["language"]==nil && all.count==5,"empty worldwide search without hidden filters")
        let next=URLComponents(url:RadioDirectory.request(name:"a&b + 音乐",chineseOnly:true,offset:10).url!,resolvingAgainstBaseURL:false)!.queryItems!
        check(next.first(where:{$0.name=="name"})?.value=="a&b + 音乐" && next.first(where:{$0.name=="offset"})?.value=="10","search escaping and ten-result pagination")
        let twenty=(0..<20).map { RadioStation(name:"Station \($0)",url:"https://example.org/\($0)") }
        var full=try! RadioListTransfer(stations:twenty,revision:1)
        _ = try! full.accept(["ok":true]);for _ in twenty { _ = try! full.accept(["ok":true]) }
        check(try! full.accept(["ok":true,"revision":2,"count":20]),"write 20 stations including index 19")
        check((try? RadioListTransfer(stations:twenty+[RadioStation(name:"21",url:"https://example.org/20")],revision:1))==nil,"reject station 21")
        check((try? RadioListTransfer(stations:twenty,revision:1,capacity:16))==nil,"honor older device capacity")
        var twentyRead=RadioListTransfer()
        for i in 0..<20 { _ = try! twentyRead.accept(["ok":true,"revision":1,"count":20,"capacity":20,"index":i,"station":twenty[i].object]) }
        check(twentyRead.stage == .done && twentyRead.stations==twenty,"read 20 stations in order")
        var empty=RadioListTransfer()
        check((try? empty.accept(["ok":true,"revision":1,"count":0]))==true && empty.stations.isEmpty,"read empty radio list")
        var transfer=try! RadioListTransfer(stations:RadioStation.presets,revision:1)
        check(transfer.command.0=="radio.begin","atomic radio begin")
        check((try? transfer.accept(["ok":true]))==false,"begin acknowledges without completing")
        for _ in RadioStation.presets { check(transfer.command.0=="radio.put","one bounded radio entry");_ = try! transfer.accept(["ok":true]) }
        check(transfer.command.0=="radio.commit","radio commit after all entries")
        check((try? transfer.accept(["ok":true,"revision":2,"count":RadioStation.presets.count]))==true && transfer.revision==2,"radio commit acknowledged")
        var clearing=try! RadioListTransfer(stations:[],revision:2);_ = try! clearing.accept(["ok":true]);check(clearing.command.0=="radio.commit","empty list skips put")
        check((try? RadioListTransfer(stations:[RadioStation.presets[0],RadioStation.presets[0]],revision:1))==nil,"reject duplicate radio URLs")
        check(!RadioStation(name:" ",url:"http://host/a").valid && !RadioStation(name:"A",url:"http://user:pass@host/a").valid,"radio name and credentials")
        var read=RadioListTransfer();_ = try! read.accept(["ok":true,"revision":1,"count":2,"index":0,"station":RadioStation.presets[0].object])
        check((try? read.accept(["ok":true,"revision":2,"count":2,"index":1,"station":RadioStation.presets[1].object]))==nil,"reject mixed list revisions")
        check(RadioMedia.audio(Data("<html>not audio</html>".utf8))==nil,"HTML is not playable radio")
        var prefs=DeviceSettings();check(prefs.valid,"default settings")
        check(prefs.brightness==50 && prefs.screen_seconds==15 && prefs.volume==80,"device defaults")
        check(prefs.weather_enabled && prefs.city=="北京" && prefs.latitude==39.9 && prefs.longitude==116.4,"Beijing weather enabled by default")
        let appearance=AppearanceSettings()
        check(appearance.mode==2 && appearance.light_start==480 && appearance.dark_start==1260,"automatic 08:00/21:00 theme default")
        let baseline=DeviceSettings();var draft=baseline;draft.brightness=55
        var remote=baseline;remote.volume=30;remote.screen_seconds=20
        let merged=draft.keepingEdits(over:remote,basedOn:baseline)
        check(merged.brightness==55 && merged.volume==30 && merged.screen_seconds==20,"incoming device settings preserve local draft and unrelated device changes")
        check(Set(merged.changes(comparedTo:remote).keys)==["brightness"],"save only locally edited fields")
        remote.brightness=60
        check(draft.keepingEdits(over:remote,basedOn:baseline).brightness==55,"same-field conflict keeps unsaved local draft")
        remote.brightness=55
        check(draft.keepingEdits(over:remote,basedOn:baseline).changes(comparedTo:remote).isEmpty,"acknowledged save clears edited fields")
        draft.brightness=56
        check(draft.keepingEdits(over:remote,basedOn:baseline).brightness==56,"typing while save is in flight remains unsaved")
        var oldWeather=baseline;oldWeather.weather_enabled=false;oldWeather.city="";oldWeather.latitude=0;oldWeather.longitude=0;oldWeather.requireWeather()
        check(oldWeather.weather_enabled && oldWeather.city=="北京" && oldWeather.latitude==39.9,"legacy weather off and empty city migrate to Beijing")
        for seconds in [3,9,10,60] { prefs.screen_seconds=seconds;check(prefs.validForSave,"new display timeout range") }
        for seconds in [61,300] { prefs.screen_seconds=seconds;check(prefs.valid && !prefs.validForSave,"legacy display settings readable but not writable") }
        prefs.screen_seconds=2;check(!prefs.valid,"display lower bound");prefs.screen_seconds=60
        prefs.focus_minutes=0;check(!prefs.valid,"timer bound");prefs.focus_minutes=25
        prefs.latitude=Double.nan;check(!prefs.valid,"finite coordinates");prefs.latitude=0
        prefs.radio_url="file:///tmp/audio";check(!prefs.valid,"radio URL scheme")
        let legacy=DeviceSettings()
        let legacyData=try! JSONEncoder().encode(legacy)
        let legacyRoundTrip=try! JSONDecoder().decode(DeviceSettings.self,from:legacyData)
        check(legacyRoundTrip.appearance==nil && legacyRoundTrip.valid,"old firmware without appearance remains compatible")
        var themed=legacy;themed.appearance=AppearanceSettings()
        for mode in 0...2 {
            themed.appearance?.mode=mode
            let roundTrip=try! JSONDecoder().decode(DeviceSettings.self,from:JSONEncoder().encode(themed))
            check(roundTrip==themed && roundTrip.valid,"appearance protocol round trip")
        }
        themed.appearance?.dark_start=480;check(!themed.validForSave,"reject equal theme times")
        themed.appearance?.dark_start=1140;themed.appearance?.light_start=1440;check(!themed.valid,"reject out of range theme time")
        themed.appearance=AppearanceSettings();themed.appearance?.mode=3;check(!themed.valid,"reject unknown theme mode")
        let store=FileManager.default.temporaryDirectory.appendingPathComponent("vibe-pairing-test-"+UUID().uuidString,isDirectory:true)
        defer { try? FileManager.default.removeItem(at:store) }
        var savedPrefs=DeviceSettings();savedPrefs.brightness=35
        let draftSession=PermissionSession(settings:savedPrefs,confirmed:DeviceSettings(),loaded:true,shortcut:"ctrl+option+b",cityQuery:"北京",
            stations:Array(RadioStation.presets.reversed()),baseline:RadioStation.presets,revision:3,capacity:20,radioLoaded:true,
            query:"音乐",chineseOnly:false,source:1,customName:"未保存电台",customURL:"https://example.org/draft",tab:4,visible:true)
        let resumeID=PermissionSessionStore.save(draftSession,directory:store)!
        let resumeFile=store.appendingPathComponent("permission-resume-\(resumeID).json")
        let resumeAttributes=try! FileManager.default.attributesOfItem(atPath:resumeFile.path)
        check((resumeAttributes[.posixPermissions] as? NSNumber)?.intValue==0o600,"permission recovery draft is private")
        let resumed=PermissionSessionStore.consume(resumeID,directory:store)!
        check(resumed.settings.brightness==35 && resumed.confirmed?.brightness==50 && resumed.stations==draftSession.stations && resumed.baseline==RadioStation.presets,"preserve unsaved settings and station order across permission reload")
        check(resumed.tab==4 && resumed.customURL==draftSession.customURL && resumed.shortcut==draftSession.shortcut && !resumed.chineseOnly,"preserve form input and navigation during permission reload")
        check(PermissionSessionStore.consume(resumeID,directory:store)==nil && !FileManager.default.fileExists(atPath:resumeFile.path),"recovery draft consumed once")
        var expiredSession=draftSession;expiredSession.created=Date(timeIntervalSinceNow:-601)
        let expiredID=PermissionSessionStore.save(expiredSession,directory:store)!
        check(PermissionSessionStore.consume(expiredID,directory:store)==nil,"reject expired permission reload")
        check(PermissionSessionStore.consume("../../pairing",directory:store)==nil,"reject permission resume path traversal")
        check(PermissionProbe.check(executable:URL(fileURLWithPath:"/does-not-exist-vibe-permission-probe"))==nil,"probe startup failure never grants permission")
        check(PermissionProbe.check() != nil,"bounded same-executable permission probe returns explicit state")
        check(PermissionRecoveryPolicy.shouldReload(current:false,fresh:false,returnedFromSettings:true,alreadyReloaded:false),"recover after settings even when child preflight caches denial")
        check(PermissionRecoveryPolicy.shouldReload(current:false,fresh:true,returnedFromSettings:false,alreadyReloaded:false),"recover stale process confirmed by fresh probe")
        check(!PermissionRecoveryPolicy.shouldReload(current:false,fresh:false,returnedFromSettings:false,alreadyReloaded:false),"no spontaneous reload on denied permission")
        check(!PermissionRecoveryPolicy.shouldReload(current:false,fresh:true,returnedFromSettings:true,alreadyReloaded:true),"at most one automatic reload")
        check(!PermissionRecoveryPolicy.shouldReload(current:true,fresh:true,returnedFromSettings:true,alreadyReloaded:false),"granted process does not need reload")
        check(PermissionRelaunch.run(parent:"invalid",resumeID:UUID().uuidString)==2,"reject invalid relaunch parent")
        check(PermissionRelaunch.run(parent:"1",resumeID:"invalid")==2,"reject invalid relaunch credentials")
        let device="abcdef123456",token=PairingStore.random()
        check(token.count==64 && token.allSatisfy({"0123456789abcdef".contains($0)}),"random pairing token format")
        check(PairingStore.load(device,directory:store)==nil,"missing pairing file")
        check(PairingStore.save(token,device:device,directory:store),"save local pairing")
        check(PairingStore.load(device,directory:store)==token,"load local pairing without Keychain")
        let file=store.appendingPathComponent("pairing.json")
        let fileAttributes=try! FileManager.default.attributesOfItem(atPath:file.path)
        let dirAttributes=try! FileManager.default.attributesOfItem(atPath:store.path)
        check((fileAttributes[.posixPermissions] as? NSNumber)?.intValue==0o600,"private pairing file permissions")
        check((dirAttributes[.posixPermissions] as? NSNumber)?.intValue==0o700,"private pairing directory permissions")
        check(!PairingStore.save("bad",device:device,directory:store),"reject malformed token")
        check(!PairingStore.save(token,device:"../wrong",directory:store),"reject malformed device")
        check(PairingStore.load(device,directory:store)==token,"failed save preserves pairing")
        try! Data("invalid".utf8).write(to:file)
        check(PairingStore.load(device,directory:store)==nil,"corrupt local file is not a valid pairing")
        check(PairingStore.save(token,device:device,directory:store) && PairingStore.load(device,directory:store)==token,"fresh pairing repairs a corrupt file")
        if let i=CommandLine.arguments.firstIndex(of:"--vectors"),CommandLine.arguments.count>i+1,
           let fixture=try? Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[i+1])),fixture.count==2004 {
            let expected=(0..<1600).map { n -> Int16 in Int16(bitPattern:UInt16(fixture[804+n*2])|UInt16(fixture[805+n*2])<<8) }
            check(VoiceCodec.decode(fixture.prefix(804))==expected,"C / Swift shared waveform vector")
        }
        if CommandLine.arguments.contains("--audio-probe") {
            let sink=AudioSink()
            do { try sink.start();sink.append([Int16](repeating:0,count:1600));Thread.sleep(forTimeInterval:0.2);sink.stop();print("BlackHole output queue: PASS") }
            catch { fputs("Audio probe failed: \(error.localizedDescription)\n",stderr);exit(1) }
        }
        print("Native Mac protocol, decoder, shortcut and configuration tests: PASS")
    }
}
