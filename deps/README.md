# deps/

Third-party libraries the project builds itself, so every machine uses the same version.

- `qtmqtt.env`: the pinned Qt MQTT repository and tag.
- `qtmqtt/`: the source clone (created by **SB: Install Dependencies**, not committed).
- `build/`: intermediate build files (not committed).
- `install/`: the built Qt MQTT library and headers. CMake finds it automatically.

Qt itself is still installed system-wide (for example `C:\Qt\6.8.0\mingw_64` or `~/Qt/6.8.0/gcc_64`).
