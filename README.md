# MS-Visualizer

A standalone **Windows 10/11 x64** projectM visualizer for a bar, live band,
or DJ setup. Built from scratch for Windows: no Raspberry Pi services,
desktop scripts, ALSA loopback, browser, or Spotify login needed.

The **[Pi-Visualizer edition](README-PI.md)** uses the same app and reactive
rendering code with Raspberry Pi GLES graphics and ALSA audio capture.

## Start

Download the Windows ZIP from [Releases](https://github.com/coreyjdl/ms-visualizer/releases),
right-click **Extract All**, and open **Start MS-Visualizer.cmd** or
**MS-Visualizer.exe**. Keep the entire folder together, including the DLLs
and `presets/`. No admin permission or Python is needed to run it.
The release contains the Visual C++ runtime DLLs needed by the app.
Windows may show an unsigned-app warning; builds are not code-signed.

Choose **Audio input**, then **Start input**. The **LIVE AUDIO** indicator
and input meter must move before troubleshooting presets. Press
**Save settings** after configuring the setup. Next launch uses your saved
device, input gain, reactive options, silence threshold, and cycle interval.
The initial default captures the default Windows speakers.

## More presets

Open **Install More Presets.cmd** in the extracted release folder to download
projectM's official **Cream of the Crop** collection: **9,795 additional presets**
and its companion MilkDrop textures. Internet access is required; pinned archives
are SHA-256 verified. Restart the visualizer afterward. The five starter presets
remain installed. Shortened filenames prevent Windows path-length problems;
`presets/Cream-of-the-Crop/preset-index.csv` preserves original names and categories.

This is an optional upstream download, not part of the application's MIT license.
The collection's upstream notice says most presets have no explicit license and
individual authors retain copyright; it assumes public-domain status based on
their longstanding free distribution. That is not an explicit license grant from
each author. The installer preserves the upstream notice and authors' names.
Some presets are GPU-heavy or incompatible with projectM; the player skips
failed loads. The installer can be run on each PC, including SSH deployments:

```powershell
.\install-presets.ps1 -AppDirectory "$env:LOCALAPPDATA\Programs\MS-Visualizer"
```

| Setup | Input to select |
| --- | --- |
| Spotify / DJ software on this PC | **System audio** for the speakers that actually play it |
| Behringer USB interface / USB mixer | **Input** for its Windows recording endpoint |
| Mixer through an analog interface | **Input** for that interface's line input |
| Room microphone / mic jack | The relevant **Input** microphone endpoint |

WASAPI runs in shared mode at 44.1 kHz stereo with device-format conversion.
Mono microphones are supported. This is **capture only**: the app never
routes input back to speakers, changes speaker volume, or creates feedback.
An ASIO-only source must expose a Windows recording endpoint or be routed
to a WASAPI-visible device. Channels beyond the first stereo pair are not
selectable in this release.

For a band, use a suitable mixer line/record output into an interface
line input. Do not connect a powered speaker/amplifier output to a USB
interface. Set hardware gain so it does not clip; app gain affects visuals
only. You may need **Settings > Privacy > Microphone > Allow desktop apps**
for recording inputs.

## Reactive behavior you can verify

**Beat-driven response** is enabled by default. A separate bass-envelope
onset detector measures the captured samples and drives projectM's displayed
zoom, glow, and pulse rings. It does not depend on a preset deciding to
use projectM's normalized beat variables. The **Detected bass / beat pulse**
meter shows those measured pulses.

**Preset-native** behavior is available by unchecking Beat-driven response.
The five original bundled MilkDrop presets also use projectM audio and
waveform data. Copy additional `.milk` files and associated textures into
`presets/`, then restart. Imported presets vary in reactivity.

**Freeze when input is silent** defaults on. After input stops or falls below
the adjustable threshold, the last frame freezes and beats clear. Automatic
preset cycling pauses during silence. No new capture callbacks for 150 ms
also clears old PCM so paused loopback audio cannot keep replaying stale
samples. Uncheck freeze to allow projectM's time-driven animation in silence.

Press **Test beats (no speaker sound)** for a known 120 BPM input signal,
including a silent interval from 4-6 seconds every eight seconds. This is
injected into the visual path only, not played through the venue speakers.
If this works but the live input meter does not move, the issue is the
selected Windows audio endpoint, permissions, or mixer connection.

Adjust **Visual input gain** for quiet recording devices, **Silence threshold**
for room noise, and **Beat effect strength** for subtle or strong pulses.
Reduce gain if the UI reports visual clipping. Bass-onset detection is not
beat-grid/BPM tracking; ambient music or music without strong bass can
produce fewer pulses. Other waveform motion still responds to input.

## Controls

| Control | Action |
| --- | --- |
| F1 | Show/hide audio setup |
| F11 | Toggle fullscreen on the current display |
| Space | Next preset |
| Esc | Show controls, not exit |
| Window close | Exit |

F1 hides the controls completely, without a permanent corner button.
F1 or Esc brings the panel back. Input errors still display an alert.
F1, F11, and Esc work even when a setup control has keyboard focus.

Drag the window onto the projector/TV display before selecting fullscreen.
Rendering is capped at 30 FPS with a 640x480 projectM render surface and
display scaling for low-power PCs. Hardware **OpenGL 3.3** is required.
Update Intel/AMD/NVIDIA graphics drivers if startup reports an OpenGL error.

## Troubleshooting

Settings and `MS-Visualizer.log` are stored under
`%LOCALAPPDATA%\MS-Visualizer`. Errors appear in the control panel, and fatal
startup errors show a Windows dialog. A disconnected saved input is reported,
not silently replaced with a different microphone.

For deployment over Windows SSH, copy the extracted folder to
`%LOCALAPPDATA%\Programs\MS-Visualizer`, then run the bundled
`start-desktop.ps1 -CreateShortcut` in PowerShell. It launches through a
limited, interactive scheduled task for the already logged-in user;
an ordinary SSH process cannot display a window on the venue desktop.
No password is saved. A Windows user must be logged in locally.

**Paused Spotify but meter still moves:** system loopback includes all audio
on the selected speakers, not only Spotify. Check other applications, and
check Spotify Connect is playing on this PC rather than another device.
Speaker mute may not mute the loopback monitor; pausing playback is the
meaningful comparison.

**Device changed/unplugged:** choose Refresh inputs, select the endpoint,
then Start input. **Stopped / Silent** is distinct from **LIVE AUDIO**.
There is no hidden demo fallback for missing hardware.

## Build and checks

Use Visual Studio 2022 with Desktop development with C++, CMake 3.24+,
Git, and vcpkg. Dependencies are pinned and restored by CMake/vcpkg.

```powershell
git clone https://github.com/coreyjdl/ms-visualizer
cd ms-visualizer
git clone https://github.com/microsoft/vcpkg "$env:TEMP\msvis-vcpkg"
git -C "$env:TEMP\msvis-vcpkg" checkout ef7dbf94b9198bc58f45951adcf1f041fcbc5ea0
& "$env:TEMP\msvis-vcpkg\bootstrap-vcpkg.bat" -disableMetrics
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:TEMP/msvis-vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

GitHub Actions builds and packages the portable Windows release.
`audio-tests` verifies bass beats versus silence, pause reset, right-only
stereo input, sample isolation, and bounded-buffer overflow.
The DSP tests also compile on Linux without Windows dependencies:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc tests/audio_tests.cpp -o /tmp/msvis-audio-tests
/tmp/msvis-audio-tests
```

Runtime diagnostic switches:
`--list-inputs` writes `inputs.json` alongside the log;
`--smoke-test` opens eight seconds of known beat/silence input and verifies
an actual rendered pixel difference with the beat effect, then identical
frames during silence. It writes `test-beat.bmp`, `test-silence.bmp`,
`smoke-test.bmp` and a pass/failure result to the log;
`--capture-test` checks the default speaker loopback for eight seconds,
reporting actual captured frames/RMS (zero RMS is not proof of a working
music source). It does not turn on a hidden sound source.
Add `--microphone` to check the default recording endpoint instead.

## License

Application and original bundled presets: MIT. projectM: LGPL-2.1-or-later,
dynamically linked and replaceable. See `THIRD-PARTY-NOTICES.md` and the
dependency source assets included with releases.
