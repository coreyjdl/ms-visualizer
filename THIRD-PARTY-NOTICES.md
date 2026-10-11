# Third-party components

MS-Visualizer and its five original bundled presets are MIT licensed.
The application dynamically links **projectM 4.1.6**, licensed under
LGPL-2.1-or-later. `projectM-LICENSE.txt` is included in the portable package.
You may replace its DLLs with compatible modified builds. No proprietary
Spotify SDK, Pi renderer, ALSA configuration, or preset collection is included.

| Component | License | Source |
| --- | --- | --- |
| projectM 4.1.6 and bundled projectm-eval | LGPL-2.1-or-later | https://github.com/projectM-visualizer/projectm/releases/tag/v4.1.6 |
| SDL2 | zlib | https://github.com/libsdl-org/SDL |
| Dear ImGui 1.91.9b | MIT | https://github.com/ocornut/imgui/tree/v1.91.9b |
| miniaudio 0.11.22 | MIT-0 or public domain | https://github.com/mackron/miniaudio/tree/0.11.22 |
| GLEW | BSD/MIT | https://github.com/nigels-com/glew |
| GLM | MIT | https://github.com/g-truc/glm |
| nlohmann/json | MIT | https://github.com/nlohmann/json |

Exact revisions and dependency versions are pinned in `CMakeLists.txt` and
`vcpkg.json`. Release packages include dependency copyright notices in
`licenses/`. Release assets also include the exact projectM source archive and
the application source needed to rebuild the DLLs and executable.
