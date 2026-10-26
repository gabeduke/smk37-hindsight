// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// A MIDI monitor and sender for the Mac, on CoreMIDI (HINDSIGHT.md):
//   swiftc -O -o build/midi tools/midi.swift
// list CoreMIDI endpoints, log what a source sends, send bytes.
//   midi list
//   midi listen <seconds> <name-substring>   (hex, one message per line)
//   midi send <name-substring> <hex bytes...>
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
default:
    print("usage: midi list | listen <seconds> <name> | send <name> <hex...>")
}
