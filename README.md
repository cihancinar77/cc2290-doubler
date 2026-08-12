# CC2290 — Dynamic Digital Doubler

> **Learning / demo project.** This is a personal educational project built to learn
> audio DSP and JUCE plugin development. It is **not** a commercial product and is
> **not affiliated with or endorsed by TC Electronic**. "TC2290" is referenced only
> to describe the classic doubling technique that inspired the DSP design; the UI is
> a from-scratch homage to the look of classic rack delays.

A free Audio Unit (AU) doubler plugin for Logic Pro (macOS), modelled after the
classic **TC Electronic TC2290** studio doubling trick:

![CC2290 UI](ui-preview.png)

## How the effect works

The famous "2290 doubling" sound is not a special mode — it is a recipe:

1. **Short delay** (10–50 ms, classic value 24 ms), single tap, no feedback.
   The ear hears this not as an echo but as a second performance.
2. **Delay-time modulation** (sine or random LFO) creates small pitch drift in the
   copy — the secret of a believable double, since no human plays the same take
   twice at identical pitch. The 2290's *automatic depth correction* keeps the
   pitch excursion constant when you change the LFO speed; this plugin does the
   same (depth is set directly in cents).
3. **Dynamics** — the 2290's envelope-driven section: the double ducks under the
   dry signal while you play (ducking), and modulation can deepen the harder you
   play (dynamic modulation).

Extras: a golden-ratio (×1.618) second voice for stereo width, optional
"vintage tone" low-pass, and an LED rack-style UI with draggable 7-segment
displays and live input/output meters.

## Default settings — the Petrucci split

The defaults follow John Petrucci's documented live use of the 2290: he runs a
**7 ms delay between left and right, 100% wet, 0% feedback** on practically every
sound, as a stereo split with light chorusing. So the plugin opens with
Delay 7 ms, Double 100%, Width 100%, Feedback 0%, no ducking, and subtle random
modulation (4 cents @ 0.35 Hz) for the chorused movement. Classic 24 ms
"vocal doubling" is one knob away — raise Delay to 24 ms.

## Parameters

| Section | Controls |
|---|---|
| MODULATION | Speed (Hz), Depth (cents) |
| DELAY | Delay (ms), Feedback (%) |
| DYNAMICS | Ducking (%), Dyn Speed (ms), Dyn Mod (%) |
| MIX | Dry (%), Double (%), Width (%) |
| MODE | Sine / Random waveform, Vintage tone |

## Building

Requirements: macOS, Xcode command-line tools, CMake.

```sh
git clone https://github.com/cihancinar77/cc2290-doubler.git
cd cc2290-doubler
git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
```

The AU component is installed to `~/Library/Audio/Plug-Ins/Components/` automatically
(`COPY_PLUGIN_AFTER_BUILD`). Verify with:

```sh
auval -v aufx Db90 Cihn
```

A standalone app is also built at `build/CC2290_artefacts/Release/Standalone/CC2290.app`,
and `UISnap` renders the UI to a PNG (used for the screenshot above).

## Status

Built as a learning exercise — expect rough edges. Issues and PRs welcome, but this
is not maintained as a production plugin.
