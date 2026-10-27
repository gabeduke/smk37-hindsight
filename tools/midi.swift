// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// A MIDI monitor and sender for the Mac, on CoreMIDI (HINDSIGHT.md):
//   swiftc -O -o build/midi tools/midi.swift
// list CoreMIDI endpoints, log what a source sends, send bytes.
//   midi list
//   midi listen <seconds> <name-substring>   (hex, one message per line)
//   midi send <name-substring> <hex bytes...>
//   midi hs <name-substring> hello | clear <colour> | release
//   midi hs <name-substring> text <x> <y> <scale> <colour> <words...>
//     sends Hindsight's message (app/hindsight.h) and times its ack
import CoreMIDI
import Foundation

func str(_ obj: MIDIObjectRef, _ key: CFString) -> String {
    var s: Unmanaged<CFString>?
    return MIDIObjectGetStringProperty(obj, key, &s) == noErr ? (s!.takeRetainedValue() as String) : "-"
}
func int(_ obj: MIDIObjectRef, _ key: CFString) -> String {
    var v: Int32 = 0
    return MIDIObjectGetIntegerProperty(obj, key, &v) == noErr ? String(v) : "-"
}
func describe(_ e: MIDIEndpointRef) -> String {
    "name=\"\(str(e, kMIDIPropertyName))\" display=\"\(str(e, kMIDIPropertyDisplayName))\" uid=\(int(e, kMIDIPropertyUniqueID)) offline=\(int(e, kMIDIPropertyOffline)) sysexSpeed=\(int(e, kMIDIPropertyMaxSysExSpeed)) driver=\(str(e, kMIDIPropertyDriverOwner))"
}
func sources() -> [MIDIEndpointRef] { (0..<MIDIGetNumberOfSources()).map { MIDIGetSource($0) } }
func destinations() -> [MIDIEndpointRef] { (0..<MIDIGetNumberOfDestinations()).map { MIDIGetDestination($0) } }
func matches(_ e: MIDIEndpointRef, _ sub: String) -> Bool {
    str(e, kMIDIPropertyDisplayName).localizedCaseInsensitiveContains(sub) ||
    str(e, kMIDIPropertyName).localizedCaseInsensitiveContains(sub)
}

var client = MIDIClientRef()
MIDIClientCreateWithBlock("midi" as CFString, &client, nil)
let args = CommandLine.arguments
let start = Date()

switch args.count > 1 ? args[1] : "list" {
case "list":
    for d in 0..<MIDIGetNumberOfDevices() {
        let dev = MIDIGetDevice(d)
        print("device \"\(str(dev, kMIDIPropertyName))\" maker=\"\(str(dev, kMIDIPropertyManufacturer))\" model=\"\(str(dev, kMIDIPropertyModel))\" offline=\(int(dev, kMIDIPropertyOffline)) driver=\(str(dev, kMIDIPropertyDriverOwner))")
        for n in 0..<MIDIDeviceGetNumberOfEntities(dev) {
            let ent = MIDIDeviceGetEntity(dev, n)
            print("  entity \"\(str(ent, kMIDIPropertyName))\"")
            for s in 0..<MIDIEntityGetNumberOfSources(ent) { print("    source " + describe(MIDIEntityGetSource(ent, s))) }
            for s in 0..<MIDIEntityGetNumberOfDestinations(ent) { print("    dest   " + describe(MIDIEntityGetDestination(ent, s))) }
        }
    }
case "listen":
    let seconds = Double(args[2]) ?? 10, sub = args[3]
    var port = MIDIPortRef()
    MIDIInputPortCreateWithBlock(client, "in" as CFString, &port) { list, src in
        let label = src.map { "\($0.load(as: Int.self))" } ?? "?"
        var p = list.pointee.packet
        for _ in 0..<list.pointee.numPackets {
            let bytes = withUnsafeBytes(of: p.data) { Array($0.prefix(Int(p.length))) }
            let t = String(format: "%8.3f", Date().timeIntervalSince(start))
            print("\(t) src\(label) " + bytes.map { String(format: "%02X", $0) }.joined(separator: " "))
            fflush(stdout)
            p = MIDIPacketNext(&p).pointee
        }
    }
    var n = 0
    for (i, s) in sources().enumerated() where matches(s, sub) {
        let ctx = UnsafeMutableRawPointer.allocate(byteCount: 8, alignment: 8)
        ctx.storeBytes(of: i, as: Int.self)
        MIDIPortConnectSource(port, s, ctx)
        print("listening: src\(i) " + describe(s)); n += 1
    }
    if n == 0 { print("no source matches \(sub)"); exit(1) }
    fflush(stdout)
    RunLoop.main.run(until: Date().addingTimeInterval(seconds))
    print(String(format: "%8.3f done", Date().timeIntervalSince(start)))
case "send":
    let sub = args[2]
    let bytes = args[3...].flatMap { $0.split(separator: " ") }.map { UInt8($0, radix: 16)! }
    guard let dest = destinations().first(where: { matches($0, sub) }) else { print("no destination matches \(sub)"); exit(1) }
    var port = MIDIPortRef()
    MIDIOutputPortCreate(client, "out" as CFString, &port)
    var list = MIDIPacketList()
    let pkt = MIDIPacketListInit(&list)
    _ = MIDIPacketListAdd(&list, MemoryLayout<MIDIPacketList>.size, pkt, 0, bytes.count, bytes)
    print("send to " + describe(dest) + ": " + bytes.map { String(format: "%02X", $0) }.joined(separator: " "))
    MIDISend(port, dest, &list)
    RunLoop.main.run(until: Date().addingTimeInterval(0.3))
case "hs" where args.count >= 4:
    let sub = args[2], seq = UInt8(Int(Date().timeIntervalSince1970 * 10) % 128)
    var frame: [UInt8] = [0xF0, 0x7D, 0x48, 0x53, 0x01, seq]
    switch args[3] {
    case "hello": frame.append(0x00)
    case "clear" where args.count == 5: frame += [0x01, UInt8(args[4])!]
    case "release": frame.append(0x03)
    case "text" where args.count >= 8:
        let n = args[4...7].map { Int($0)! }, words = args[8...].joined(separator: " ")
        frame += [0x02, UInt8(n[0] & 127), UInt8(n[0] >> 7), UInt8(n[1] & 127), UInt8(n[1] >> 7), UInt8(n[2]), UInt8(n[3])]
        frame += Array(words.utf8)
    default: print("usage: midi hs <name> hello | clear <colour> | release | text <x> <y> <scale> <colour> <words...>"); exit(2)
    }
    frame.append(0xF7)
    guard let dest = destinations().first(where: { matches($0, sub) }),
          let source = sources().first(where: { matches($0, sub) }) else { print("no port matches \(sub)"); exit(1) }
    var ack: [UInt8]? = nil
    var input = MIDIPortRef()
    MIDIInputPortCreateWithBlock(client, "in" as CFString, &input) { list, _ in
        var p = list.pointee.packet
        for _ in 0..<list.pointee.numPackets {
            let bytes = withUnsafeBytes(of: p.data) { Array($0.prefix(Int(p.length))) }
            if bytes.count >= 9 && bytes[0] == 0xF0 && bytes[1] == 0x7D && bytes[2] == 0x48 && bytes[3] == 0x53 &&
               bytes[5] == seq && bytes[6] == 0x7F { ack = bytes }
            p = MIDIPacketNext(&p).pointee
        }
    }
    MIDIPortConnectSource(input, source, nil)
    var output = MIDIPortRef()
    MIDIOutputPortCreate(client, "out" as CFString, &output)
    var list = MIDIPacketList()
    let pkt = MIDIPacketListInit(&list)
    _ = MIDIPacketListAdd(&list, MemoryLayout<MIDIPacketList>.size, pkt, 0, frame.count, frame)
    print("send: " + frame.map { String(format: "%02X", $0) }.joined(separator: " "))
    let sent = Date()
    MIDISend(output, dest, &list)
    while ack == nil && Date().timeIntervalSince(sent) < 1 { RunLoop.main.run(until: Date().addingTimeInterval(0.001)) }
    guard let reply = ack else { print("no ack within a second: not our firmware, or not listening"); exit(1) }
    let names = ["ok", "unknown command", "malformed", "screen list full", "released: the screen is the keyboard's; CLEAR takes it"]
    print(String(format: "ack in %.1f ms: ", Date().timeIntervalSince(sent) * 1000) +
          (Int(reply[7]) < names.count ? names[Int(reply[7])] : "status \(reply[7])") +
          (reply.count >= 11 ? ", protocol \(reply[8]), \(reply[9]) items" : ""))
default:
    print("usage: midi list | listen <seconds> <name> | send <name> <hex...> | hs <name> ...")
}
