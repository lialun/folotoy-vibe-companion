import Foundation

func check(_ condition:Bool,_ description:String) {
    if !condition {fputs("FAIL: \(description)\n",stderr);exit(1)}
}
VoiceMergeTests.run(check)
VoiceRepairTests.run(check)
VoiceSessionTests.run(check)
VoiceFeedbackTests.run(check)
InputCommandTests.run(check)
BLEFrameTests.run(check)
print("Production Swift voice: carrier timelines, repair, session lifecycle, explicit input and PCM ring PASS")
