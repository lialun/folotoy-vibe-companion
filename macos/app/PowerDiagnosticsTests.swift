import Foundation

enum PowerDiagnosticsTests {
    static func run(_ check:(Bool,String)->Void) {
        let body:[String:Any]=["elapsed_ms":70000,"blank_ms":60000,"blank_sleep_ms":54000,
            "sleep_ms":55000,"longest_ms":97,"sleeps":1000,"rejects":1,"usb_host":true,"key_wake":true,
            "battery":84,"millivolts":4000,"heap":["used":204800,"free":40960,"largest":24576,"min_free":8192],
            "locks":"i2s_driver APB_FREQ_MAX active=0 held_ms=52671\ni2s_driver APB_FREQ_MAX active=0 held_ms=52660\nwifi APB_FREQ_MAX active=1 held_ms=80000\nvibe_screen NO_LIGHT_SLEEP active=2 held_ms=10000\n"]
        let s=PowerSnapshot(body,received:Date(timeIntervalSince1970:0)),report=s.textReport
        check(s.ratioText=="90.0%" && s.blankAwakeMS==6000 && s.screenOnMS==10000,"diagnostics distinguish blank awake, blank sleep and screen-on intervals")
        check(s.averageMS==55 && PowerDiagnostics.milliseconds(s.longestMS)=="97.0 毫秒","short sleeps retain millisecond precision")
        check(s.locks.count==3 && s.locks.first?.name=="vibe_screen" && s.locks.first?.activeCount==1,"active no-sleep locks appear first; recursive counts are not multiple locks")
        let audio=s.locks.first{$0.name=="i2s_driver"}
        check(audio?.locks.count==2 && audio?.heldText=="52.7 秒 ×2" && audio?.activeCount==0,"paired audio locks group without adding overlapping hold durations")
        check(report.contains("52671") && report.contains("52660") && report.contains("1970-01-01") && report.contains("battery_mv=4000"),"copied report preserves exact lock data and snapshot timestamp")
        check(report.contains("sleep_ms=55000") && report.contains("elapsed_ms=70000") && report.contains("used=204800 free=40960 largest=24576 min_free=8192"),"model-facing report retains raw milliseconds and bytes without display rounding")
        check(!report.contains("口径") && !report.contains("heap不含") && report.split(separator:"\n").count==10,"source-aware report contains measurement data without explanatory paragraphs")
        check(report.contains("usb_host=true key_wake=true") && report.contains("locks_truncated=false") && s.lockReportStatus=="完整","reported USB and lock-completeness data remain visible and copyable")
        check(s.heap?.free==40960 && s.heap?.largest==24576 && s.heap?.minimum==8192 && PowerDiagnostics.bytes(s.heap?.used)=="200.0 KiB","heap total, contiguous allocation limit and historical low are distinct")
        check(abs((s.heap?.usedRatio ?? 0)-5.0/6)<0.00001,"heap usage uses allocated plus available bytes, not nominal chip RAM")
        let old=PowerSnapshot(["sleeps":0,"sleep_ms":0,"blank_ms":0])
        check(old.heap==nil && old.blankRatio==nil && old.averageMS==nil && old.textReport.contains("used=null") && old.lockReportStatus=="未提供","older firmware and zero-duration test windows degrade without fabricated measurements")
        let invalid=PowerSnapshot(["sleeps":Double.infinity,"rejects":Double.greatestFiniteMagnitude,
            "battery":101,"millivolts":-1,"blank_ms":10,"blank_sleep_ms":20,"heap":["free":-1,"largest":Double.nan]])
        check(invalid.sleeps==nil && invalid.rejects==nil && invalid.battery==nil && invalid.heap==nil && invalid.blankRatio==nil && invalid.blankAwakeMS==nil,"invalid counters and inconsistent intervals cannot overflow or imply healthy measurements")
        let malformed=PowerSnapshot(["locks":"unrecognized row\nlock NO_LIGHT_SLEEP active=-1 held_ms=12\nlock CPU_FREQ_MAX active=0 held_ms=nan\n","locks_truncated":true])
        check(malformed.unparsedLocks && malformed.locksTruncated && malformed.locks.isEmpty && malformed.textReport.contains("unrecognized row"),"malformed or truncated locks preserve raw evidence and warn instead of silently disappearing")
        let empty=PowerSnapshot(["locks":"","locks_truncated":false,"usb_host":false])
        check(empty.lockReportStatus=="完整" && empty.textReport.contains("usb_host=false") && empty.textReport.contains("locks_reported=true"),"an empty successful lock report and disconnected USB are distinct from missing data")
    }
}
