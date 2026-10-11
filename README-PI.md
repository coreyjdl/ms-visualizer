# Pi-Visualizer

Raspberry Pi edition of MS-Visualizer, built from the same projectM player,
audio meter, bass-onset detector, reactive presentation shader, and control panel.
Uses **OpenGL ES 3** and **ALSA capture** instead of Windows OpenGL/WASAPI.
The old native renderer is not included.

F1 hides all controls; F1 or Esc restores them. F11 toggles fullscreen.
Choose an input, Start input, then Save settings. Gain affects visuals only.
Settings and `Pi-Visualizer.log` live in `~/.config/Pi-Visualizer/`.
The default system-audio input is the existing `visualizer_monitor` ALSA tap;
Spotify and PipeWire routing must already provide that tap.
Microphones and USB interfaces can be selected from ALSA input hints.
This app does not play audio or change speaker volume.

The same selectable beat-driven zoom/glow/rings and freeze-on-silence behavior
as Windows is enabled by default. Presets load recursively from `build/presets/`.
Additional presets and textures can be placed beneath that folder.
`python3 scripts/install-presets.py` installs the same optional 9,795-preset
collection and 67 textures as the Windows installer, with SHA-256 checks
and upstream copyright notices. It requires Python 3.11+ only for installation;
the visualizer itself does not use Python. See the upstream rights caveat in
the main README before redistributing this collection.
Some presets need desktop-only shaders or are too expensive for Pi graphics;
failed preset loads are skipped by the playlist.

## Build

Requires SDL2, ALSA, GLES3, nlohmann/json, CMake, Git, a C++17 compiler, and a
**GLES-enabled projectM 4.1+ with its playlist library**. Debian's older projectM
3 package is not compatible; retain the GLES 4.1.6 library already built on the Pi.

```sh
sudo apt-get install cmake git g++ libsdl2-dev libasound2-dev libgles-dev nlohmann-json3-dev
cmake -S . -B build -DPI_VISUALIZER=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/Pi-Visualizer
```

`--smoke-test` verifies measured pixel changes from synthetic beats and identical
frames during silence without playing audio. `--capture-test` checks the actual
Spotify monitor; music must be playing to verify a real signal.
`--list-inputs` writes the ALSA input list. `--fullscreen --hide-controls` launches
with a clean fullscreen display. Use F1 to select an input and save settings.

After installing in `~/pi-visualizer`, the user service template in
`scripts/pi-visualizer.service` provides desktop startup. Stop and disable the old
renderer service before enabling this one. Keep the existing Spotify/monitor
services; replacing them would interrupt audio playback.
