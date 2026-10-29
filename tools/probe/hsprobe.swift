// hsprobe <name-substring> <file>: sends each line of <file> (hex bytes, or
// "wait <seconds>", or "# comment") to the first MIDI destination matching the
// name, as SysEx, and prints everything the matching source sends back.
import CoreMIDI
import Foundation

let args = CommandLine.arguments
let sub = args[1].lowercased()
let lines = try! String(contentsOfFile: args[2], encoding: .utf8).split(separator: "\n")
let start = Date()
func name(_ e: MIDIEndpointRef) -> String {
    var s: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &s)
    return (s?.takeRetainedValue() as String?) ?? "?"
}
var client = MIDIClientRef()
MIDIClientCreate("hsprobe" as CFString, nil, nil, &client)
var inPort = MIDIPortRef()
MIDIInputPortCreateWithBlock(client, "in" as CFString, &inPort) { list, _ in
    var p = list.pointee.packet
    for _ in 0..<list.pointee.numPackets {
        let bytes = withUnsafeBytes(of: p.data) { Array($0.prefix(Int(p.length))) }
        print(String(format: "%7.3f <- ", Date().timeIntervalSince(start)) + bytes.map { String(format: "%02X", $0) }.joined(separator: " "))
        fflush(stdout)
        p = MIDIPacketNext(&p).pointee
    }
}
for i in 0..<MIDIGetNumberOfSources() {
    let s = MIDIGetSource(i)
    if name(s).lowercased().contains(sub) { MIDIPortConnectSource(inPort, s, nil) }
}
var dest: MIDIEndpointRef = 0
for i in 0..<MIDIGetNumberOfDestinations() where name(MIDIGetDestination(i)).lowercased().contains(sub) {
    dest = MIDIGetDestination(i); break
}
if dest == 0 { print("no destination matches \(sub)"); exit(1) }
print("to " + name(dest))
for line in lines {
    let t = line.trimmingCharacters(in: .whitespaces)
    if t.isEmpty { continue }
    if t.hasPrefix("#") { print(String(format: "%7.3f ", Date().timeIntervalSince(start)) + t); fflush(stdout); continue }
    if t.hasPrefix("wait ") { RunLoop.main.run(until: Date().addingTimeInterval(Double(t.dropFirst(5))!)); continue }
    let bytes = t.split(separator: " ").map { UInt8($0, radix: 16)! }
    let buf = UnsafeMutablePointer<UInt8>.allocate(capacity: bytes.count)
    buf.initialize(from: bytes, count: bytes.count)
    let req = UnsafeMutablePointer<MIDISysexSendRequest>.allocate(capacity: 1)
    req.initialize(to: MIDISysexSendRequest(destination: dest, data: UnsafePointer(buf), bytesToSend: UInt32(bytes.count), complete: DarwinBoolean(false), reserved: (0, 0, 0), completionProc: { _ in }, completionRefCon: nil))
    MIDISendSysex(req)
    print(String(format: "%7.3f -> %d bytes ", Date().timeIntervalSince(start), bytes.count) + bytes.prefix(12).map { String(format: "%02X", $0) }.joined(separator: " "))
    fflush(stdout)
    while !req.pointee.complete.boolValue { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
    RunLoop.main.run(until: Date().addingTimeInterval(0.4))
}
RunLoop.main.run(until: Date().addingTimeInterval(1))
