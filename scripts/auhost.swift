// A minimal offline host that loads Everything EQ in-process (AUv3 'EvE3'
// or AUv2 'EvEQ'), renders a 440 Hz sine through it with Output Gain at
// -12 dB, and checks latency reporting, host bypass and state restore.
// Messages the plug-in prints to stderr show up here, which they don't when
// Logic hosts an AUv3 out of process.
//
//   xcrun swiftc -O scripts/auhost.swift -o build-auv3/auhost
//   build-auv3/auhost EvE3 [bypass|state|mono|sc]
//
// Expect a peak of about 0.126 (0.5 with bypass) on both channels, latency
// 0.005 s after Lookahead is set to 5 ms, and the restored values to match.
// `mono` feeds a mono input bus and a silent sidechain (mono -> stereo, as
// on a mono Logic track); `sc` puts a quieter 1 kHz sine on that sidechain.
import AVFoundation

func fourcc(_ s: String) -> FourCharCode {
  s.utf8.reduce(0) { ($0 << 8) | FourCharCode($1) }
}

let args = CommandLine.arguments
let subtype = args.count > 1 ? args[1] : "EvE3"
let desc = AudioComponentDescription(
  componentType: kAudioUnitType_Effect, componentSubType: fourcc(subtype),
  componentManufacturer: fourcc("Tulr"), componentFlags: 0, componentFlagsMask: 0)

let sem = DispatchSemaphore(value: 0)
var unit: AVAudioUnit?
AVAudioUnit.instantiate(with: desc, options: [.loadInProcess]) { u, e in
  if let e = e { print("instantiate error:", e) }
  unit = u
  sem.signal()
}
sem.wait()
guard let au = unit else { print("no unit"); exit(1) }
print("loaded:", au.auAudioUnit.audioUnitName ?? "?", "inputs:", au.auAudioUnit.inputBusses.count)

let mode = args.count > 2 ? args[2] : ""
let fmt = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
let inFmt = AVAudioFormat(standardFormatWithSampleRate: 48000,
                          channels: mode == "mono" || mode == "sc" ? 1 : 2)!
let engine = AVAudioEngine()
try engine.enableManualRenderingMode(.offline, format: fmt, maximumFrameCount: 512)
let player = AVAudioPlayerNode()
engine.attach(player)
engine.attach(au)
engine.connect(player, to: au, format: inFmt)
// Like Logic, `mono` also connects the sidechain bus (here, to silence);
// `sc` plays a quieter 1 kHz sine on it.
let scPlayer = AVAudioPlayerNode()
if mode == "mono" || mode == "sc" {
  engine.attach(scPlayer)
  engine.connect(scPlayer, to: au, fromBus: 0, toBus: 1, format: inFmt)
}
engine.connect(au, to: engine.mainMixerNode, format: fmt)
try engine.start()
print("bus formats: in", au.auAudioUnit.inputBusses[0].format.channelCount, "out", au.auAudioUnit.outputBusses[0].format.channelCount)

let n: AVAudioFrameCount = 48000
let sine = AVAudioPCMBuffer(pcmFormat: inFmt, frameCapacity: n)!
sine.frameLength = n
for c in 0..<Int(inFmt.channelCount) {
  for i in 0..<Int(n) { sine.floatChannelData![c][i] = 0.5 * sinf(Float(i) * 2 * .pi * 440 / 48000) }
}
player.scheduleBuffer(sine, completionHandler: nil)
player.play()
if mode == "sc" {
  let scBuf = AVAudioPCMBuffer(pcmFormat: inFmt, frameCapacity: n)!
  scBuf.frameLength = n
  for i in 0..<Int(n) { scBuf.floatChannelData![0][i] = 0.25 * sinf(Float(i) * 2 * .pi * 1000 / 48000) }
  scPlayer.scheduleBuffer(scBuf, completionHandler: nil)
}
if mode == "mono" || mode == "sc" { scPlayer.play() }
func param(_ unit: AUAudioUnit, _ name: String) -> AUParameter? {
  unit.parameterTree?.allParameters.first(where: { $0.displayName == name })
}
if let g = param(au.auAudioUnit, "Output Gain") { g.value = -12 }
if mode == "bypass" { au.auAudioUnit.shouldBypassEffect = true }
// State: save, load into a second instance, compare.
if mode == "state" {
  let state = au.auAudioUnit.fullState
  var second: AVAudioUnit?
  AVAudioUnit.instantiate(with: desc, options: [.loadInProcess]) { u, _ in second = u; sem.signal() }
  sem.wait()
  second!.auAudioUnit.fullState = state
  RunLoop.main.run(until: Date().addingTimeInterval(0.5))
  for name in ["Output Gain", "Lookahead"] {
    print("restored", name, param(second!.auAudioUnit, name)?.value ?? -999,
          "original", param(au.auAudioUnit, name)?.value ?? -999)
  }
}

// Latency: observe KVO, then change Lookahead.
var kvoCount = 0
let obs = au.auAudioUnit.observe(\.latency, options: [.new]) { _, _ in kvoCount += 1 }
if let p = au.auAudioUnit.parameterTree?.allParameters.first(where: { $0.displayName == "Lookahead" }) {
  print("lookahead param:", p.value, p.unitName ?? "")
  p.value = 5
  print("readback:", p.value, "min", p.minValue, "max", p.maxValue, "addr", p.address)
}
RunLoop.main.run(until: Date().addingTimeInterval(0.5))
print("latency after lookahead=5:", au.auAudioUnit.latency, "kvo:", kvoCount)
_ = obs

let out = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat, frameCapacity: 512)!
var peak: [Float] = [0, 0]
for block in 0..<20 {
  let status = try engine.renderOffline(512, to: out)
  if status != .success { print("block", block, "status", status.rawValue) }
  for c in 0..<2 {
    for i in 0..<Int(out.frameLength) { peak[c] = max(peak[c], abs(out.floatChannelData![c][i])) }
  }
}
print("peak L:", peak[0], "R:", peak[1], "latency(s):", au.auAudioUnit.latency)
