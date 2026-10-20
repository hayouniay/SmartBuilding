# Smart Building Monitoring and Control System

MQTT + C++23 + Qt 6 system with three programs:

| Program        | Role                                                                 |
|----------------|----------------------------------------------------------------------|
| `sb_simulator` | Simulated sensors (temperature, humidity) and actuators (HVAC, light) |
| `sb_server`    | Subscribes to everything, keeps device registry, raises alerts, logs to SQLite |
| `sb_client`    | Qt widget dashboard: zones, gauges, trends, device table, controls, alerts |

## 1. Environment (WSL Ubuntu 24.04)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git \
    gcc-14 g++-14 mosquitto mosquitto-clients \
    qt6-base-dev libqt6sql6-sqlite
```

C++23 needs GCC 14 on Ubuntu 24.04 (the default GCC 13 is not enough).

Qt MQTT module: check whether your apt has it.

```bash
apt-cache search qt6 | grep -i mqtt
```

If it prints `qt6-mqtt-dev`, install it with `sudo apt install qt6-mqtt-dev`. Otherwise install Qt 6.8 LTS with the
Qt online installer (or `aqtinstall`) and tick the **Qt MQTT** module, then pass
`-DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.x/gcc_64` to CMake.

WSL GUI: Windows 11 includes WSLg, so `sb_client` opens as a normal window. On Windows 10 install an X server.

## 2. Broker

Mosquitto 2.x only accepts anonymous connections from localhost by default. For local development, allow them on port 1883:

```bash
sudo tee /etc/mosquitto/conf.d/smartbuilding.conf >/dev/null <<'EOF'
listener 1883
allow_anonymous true
EOF
sudo systemctl restart mosquitto || sudo service mosquitto restart
mosquitto_sub -h localhost -t 'smartbuilding/#' -v     # optional: watch traffic
```

## 3. Build (from VS Code)

Open the `SmartBuilding` folder from WSL with `code .`. Install the **WSL**, **C/C++**, and **CMake Tools** extensions. In the CMake Tools kit selector, choose GCC 14. Or build from a terminal:

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_CXX_COMPILER=g++-14 \
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## 4. Run (three terminals)

```bash
./build/sb_server                 # terminal 1: backend, writes smartbuilding.db
./build/sb_simulator              # terminal 2: fake devices
./build/sb_client                 # terminal 3: dashboard
```

Optional flags: `--broker <host> --port <port>` on each program, and `--database <file>` on the server.

## 5. Try it

1. Select **F2-ServerRoom** in the zone list. Its temperature rises above 26 °C because its heat load is high, so a warning appears in the alerts panel.
2. Click the HVAC row for that zone (`HVAC-F2-ServerRoom`), then press **Turn on** and **Apply setpoint**. The simulator updates its physics and the state comes back retained.
3. Stop the simulator with Ctrl+C. After about 30 s the server marks its devices offline and raises alerts.
4. Inspect history: `sqlite3 smartbuilding.db 'select * from alerts order by id desc limit 5;'` (install with `sudo apt install sqlite3`).

## Topic contract

```
smartbuilding/<zone>/<deviceId>/telemetry   {"deviceId","zone","type","unit","value","timestamp"}
smartbuilding/<zone>/<deviceId>/status      {"deviceId","zone","type","firmware","online","timestamp"}   retained
smartbuilding/<zone>/<deviceId>/state       {"deviceId","zone","type","powered","setpoint","timestamp"} retained
smartbuilding/<zone>/<deviceId>/command     {"action":"power"|"setpoint","value":number}
smartbuilding/alerts                        {"severity","zone","deviceId","message","timestamp"}
```

Retained status and state mean a client that connects later still receives the latest device state immediately.

## Project layout

```
SmartBuilding/
├── CMakeLists.txt
├── common/          MqttTopics.h, MessageParser.h (shared, header-only)
├── server/          MqttServer, DeviceManager, DatabaseManager, main
├── client/          MainWindow, MqttClient, Widgets.h (gauge + trend), main
└── simulator/       SensorSimulator, main
```

## Testing

The core logic (reconnect backoff, threshold hysteresis, automation, silence detection) has
dependency-free unit tests that do not need Qt:

```bash
g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror -I. tests/test_core.cpp -o sb_unit_tests
./sb_unit_tests          # prints "47 checks, 0 failed"
```

Or with CMake: `cmake -S . -B build -DSB_BUILD_APPS=OFF && cmake --build build && ctest --test-dir build`.

The Qt applications (server, client, simulator) are not covered by automated tests yet. They
need an integration test against a local Mosquitto broker, which is on the roadmap.

## Roadmap status

Legend: **Done** means implemented, with the logic covered by unit tests. **Wired** means the Qt code
uses the logic but has not been compiled against Qt in the environment where it was written, so
it still needs a build and a run. **Partial** means only part of the item exists. **Not started**
means it is not implemented yet.

### 1. Dashboard and UI
- Real-time sensor charts: **Done** (temperature and humidity trends; live updates)
- Room-by-room monitoring views: **Partial** (zone list filters the table, gauges, and trends)
- Dark mode and light mode: Not started
- Historical data filtering by date and device: Not started
- Customizable dashboard widgets: Not started

### 2. Smart sensors and devices
- Simulated temperature and humidity sensors: **Done**
- CO2 sensor simulation: **Wired** (simulated, with automation tied to it)
- Air-quality and smoke sensor simulation: Not started
- Battery and signal-strength indicators: Not started
- Configurable sampling intervals: Not started (the simulator tick is fixed at 2 s)
- Device discovery and registration: Not started

### 3. Alerts and automation
- Configurable thresholds: **Partial** (thresholds are defined in `defaultThresholds()`; loading them from a file is not done)
- Hysteresis on threshold alerts, with recovery notices: **Done**
- Critical vs. warning levels: **Done**
- Automatic fan control from CO2: **Done** (logic tested; wired to MQTT commands)
- Rule-based automation: **Partial** (one rule type: sensor above or below a limit, switching an actuator in the same zone)
- Visual alerts: **Partial** (colored alert list)
- Audible alerts: Not started
- Alert acknowledgement and history: Not started (history is stored in SQLite; no acknowledgement)
- Automatic light control: Not started

### 4. MQTT communication
- Automatic reconnection with exponential backoff: **Wired** (`common/Backoff.h`, tested; used in server, client, and simulator)
- Missing-reading detection: **Done** (`SilenceDetector`, tested; devices silent for 20 s are reported)
- Last Will and Testament: Not started (needs one connection per device; the simulator currently shares one)
- Configurable QoS: Not started (QoS is fixed per message type)
- Persistent sessions: Not started
- Retained status and state messages: **Done**
- Configurable topics and message formats: Not started
- Authentication and TLS: Not started

### 5. Database and reporting
- SQLite historical storage: **Done** (telemetry, alerts, commands)
- CSV export, daily and monthly reports, retention cleanup: Not started
- Missing-reading detection: **Done** (see above; it runs in memory, not as a report)

### 6. Device control
- Remote light on/off: **Done**
- Adjustable target temperature: **Done**
- Fan on/off: **Done**
- Fan speed control: Not started (fan is on/off only)
- Manual vs. automatic mode: Not started (automation can be overridden by a manual command, but the override lasts only until the next automation change)
- Record commands and device responses: **Partial** (commands are recorded; device responses are not)

### 7. Reliability and performance
- Unit tests for MQTT messages and sensor logic: **Partial** (rule and backoff logic tested; message parsing is not, because it depends on Qt)
- Integration tests with a local Mosquitto broker: Not started
- Continuous integration (GitHub Actions): Not started
- Structured logging, graceful shutdown, broker health monitoring: Not started

### 8. Security
- Authentication, TLS, topic ACLs, credentials outside source: Not started
- Input validation: **Partial** (malformed messages are rejected and logged)

### 9. Advanced
- All items: Not started

## Notes and next steps

The recommended order was: connectivity and simulation, live dashboard and storage, reconnection and offline
status, threshold alerts and automation, charts and reporting, then security, testing, and documentation.
Steps 1 to 4 are largely in place. Next: build the Qt applications and fix any compile errors, add a Mosquitto
integration test, then reporting and CSV export.

Alert thresholds and automation rules are hard-coded in `common/Rules.h`. Moving them into a JSON config file
is a natural next step.

## VS Code tasks

`.vscode/tasks.json` provides tasks for building, testing, running, debugging, and inspecting the
database (open with Terminal > Run Task, or Ctrl+Shift+B for the default build).

| Group | Examples |
|-------|----------|
| Build | Build, Build Debug, Build Release, Build Tests Only (no Qt) |
| Test | Run Tests, Run Single Test (asks for a ctest name), Run Tests (no Qt build) |
| CMake | CMake Configure, Configure Release, Configure Tests Only, Clean, Clean and Build |
| Broker | Start Broker, Stop Broker, Broker Status, Watch MQTT Traffic |
| Run | Run Server, Run Simulator, Run Client, Launch All (runs `scripts/launch-all.sh`) |
| Debug | Debug Server (GDB), Debug Client (GDB), Debug Unit Tests (GDB) |
| Database | Tables, Schema, Shell, Recent Alerts, Backup, Reset |
| Utilities | Show Build Info, Check Qt MQTT Package, Install Dependencies |

The tasks use `g++-14` (required for C++23) and assume the broker runs on `localhost:1883`.
Run "Install Dependencies" once on a fresh WSL install. Run "Check Qt MQTT Package" if the
build cannot find Qt's MQTT module.

## Install Qt 6.5+ (required)

Ubuntu 22.04 ships Qt 6.2 and Ubuntu 24.04 ships Qt 6.4. Neither is new enough, and neither apt
package set includes the Qt MQTT module. Install Qt with `aqtinstall` instead:

```bash
sudo apt install -y python3-pip
pip install --user aqtinstall
aqt list-qt linux desktop --modules 6.8.0 linux_gcc_64    # check the module names first
aqt install-qt linux desktop 6.8.0 linux_gcc_64 -O ~/Qt -m qtmqtt
```

Then configure the project with the new Qt on the prefix path:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.0/gcc_64 -DCMAKE_CXX_COMPILER=g++-14
```

C++23 also needs GCC 14. On Ubuntu 22.04, add the toolchain PPA first:
`sudo add-apt-repository ppa:ubuntu-toolchain-r/test && sudo apt install gcc-14 g++-14`.

To check your versions: `lsb_release -a`, `g++ --version`, and `cmake --version`.

## Windows 11

The project builds natively with Visual Studio 2022 (MSVC). WSL is not needed.

### 1. Install the tools

- **Visual Studio 2022 Community** (17.8 or newer), with the workload "Desktop development with C++".
  It includes MSVC, the Windows SDK, and CMake.
- **Qt 6.8 LTS, MSVC 2022 64-bit**, installed with the Qt online installer or `aqtinstall`:
  ```powershell
  pip install aqtinstall
  aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt
  ```
  Then set the path, once, in your user environment:
  ```powershell
  setx QT_DIR C:\Qt\6.8.0\msvc2022_64
  ```
  Open a new terminal afterward so the variable is visible.
- **Qt MQTT module.** Open the Qt Maintenance Tool and check whether "Qt MQTT" is listed under your
  Qt 6.8 version. If it is, tick it and install. If it isn't, build it from source with the same
  CMake steps as on Linux, using the "x64 Native Tools" prompt and the same `-DCMAKE_PREFIX_PATH=%QT_DIR%`.
- **Mosquitto broker.** Install the Windows build from mosquitto.org. Create `mosquitto.conf` in the
  Mosquitto folder with these two lines, then start it with `mosquitto -c mosquitto.conf -v`:
  ```
  listener 1883
  allow_anonymous true
  ```

### 2. Build and run

From the project folder in PowerShell:

```powershell
.\scripts\windows.ps1 configure          # generates the Visual Studio solution in build\win
.\scripts\windows.ps1 build              # Debug (use -Config Release for Release)
.\scripts\windows.ps1 test               # runs sb_unit_tests
.\scripts\windows.ps1 run-all            # server and simulator in their own windows, client in front
```

Each build copies the Qt DLLs and plugins next to the executables (via `windeployqt`), so the programs
run without changing `PATH`.

If you don't have Qt yet, you can still build and run the unit tests:

```powershell
.\scripts\windows.ps1 configure -NoQt
.\scripts\windows.ps1 build -NoQt
.\scripts\windows.ps1 test -NoQt
```

### 3. VS Code on Windows

Open the project folder in VS Code and install the **CMake Tools** and **C/C++** extensions. Select the
`win-msvc` configure preset from `CMakePresets.json`, then build from the status bar. The `.vscode/tasks.json`
tasks are written for Linux (bash, apt, systemctl) and don't run on Windows. Use the CMake Tools buttons or
`scripts\windows.ps1` instead.

### Notes

- Unicode text such as `°C` works because the build uses `/utf-8`.
- Release builds deploy the Release Qt DLLs. Debug builds deploy the Debug DLLs (`--debug`), which the Qt
  installer includes.
- If `windeployqt` is not found, the build still works, but you need to copy the Qt DLLs yourself.

## VS Code tasks (Windows and Linux)

The tasks in `.vscode/tasks.json` run the same steps on both systems. Each task calls `scripts/sb.sh` on Linux
and `scripts/sb.ps1` on Windows. Open the command palette and run **Tasks: Run Task**:

| Task | What it does |
|------|--------------|
| SB: Check Environment | Prints the version of each tool and stops if a required one is missing |
| SB: Install Dependencies | Installs missing tools, Qt 6.8 and Qt MQTT |
| SB: Build | Checks the environment, then builds the apps and unit tests (Ctrl+Shift+B) |
| SB: Build Tests Only | Builds the unit tests without Qt |
| SB: Run Tests | Builds, then runs the unit tests |
| SB: Build, Test and Launch | Builds, runs the unit tests, then launches the chosen app |
| SB: Launch App | Launches client, server, simulator, or all (after a build) |
| SB: Clean | Deletes the build folders |
| SB: Start / Stop Broker, Broker Status | Controls the Mosquitto broker |

Windows uses the MinGW compiler and Qt 6.8 MinGW (no Visual Studio), at `C:\Qt\6.8.0\mingw_64` and
`C:\Qt\Tools\mingw1310_64`. Linux uses `g++-14` and Qt at `~/Qt/6.8.0/gcc_64`. Change either location with the
`SB_QT_PREFIX` / `SB_MINGW_BIN` environment variables.

Run the script directly if you prefer the terminal: `bash scripts/sb.sh check` on Linux, or
`.\scripts\sb.ps1 check` on Windows.

## Dependencies (deps/)

Qt MQTT is no longer installed into the Qt folder. The **SB: Install Dependencies** task clones the
version pinned in `deps/qtmqtt.env`, builds it, and installs it into `deps/install`. CMake finds it
there automatically, so the project always uses the same Qt MQTT version. Qt itself is still installed
system-wide, and you can ignore the manual Qt MQTT steps earlier in this README.

To change the pinned version, edit `QTMQTT_TAG` in `deps/qtmqtt.env` and run the install task again,
or delete `deps/install` and `deps/qtmqtt` first. Keep the tag in step with your Qt version.

## Dashboard features (history, health, settings, floor plan, reports)

**Time range and device filter.** The toolbar offers the last hour, 6 hours, 24 hours, 7 days, 30 days, or custom dates. The Devices tab filters by type and zone.

**Device health.** Each sensor publishes battery level (%) and signal strength (dBm) every 30 seconds. The Devices tab shows both, coloured by level. Low battery and weak signal raise alerts through the same threshold rules as the measurements.

**Shared thresholds.** `config/thresholds.json` holds every limit and the fan automation rule. The server reads it at start-up (`--thresholds <path>` to choose another file), publishes it as a retained message, and saves any change made in the **Settings** tab. Every open dashboard receives the change at once, and the trend lines and KPI colours follow the same limits.

**Notifications.** A critical alert beeps (use **Sound: on/muted** in the header to silence it). When the dashboard is in the background, a critical alert also shows a system tray balloon.

**Themes.** **Light theme / Dark theme** in the header switches the whole dashboard. The choice is remembered for the next start.

**Floor plan.** The **Floor plan** tab draws each room from `config/floorplan.json` (positions are fractions of the plan, 0 to 1). Rooms are coloured by status. Clicking a room opens its trends. Zones that are not listed in the file are not drawn.

**Exports and reports.**
- **Export trends CSV** (Overview): the selected zone's temperature, humidity and CO2 history.
- **Export CSV** (Alerts): the alerts currently shown, with acknowledgement details.
- **Report (loaded range)** (Overview): an HTML page with per-zone peaks, lows, and alert counts for the range loaded. For a daily report choose *Last 24 hours*; for a monthly one choose *Last 30 days* or custom dates.

**Known limits.** Threshold changes apply to new readings only. Restart the server to reload the file by hand. The floor plan layout is read when the dashboard starts. Reports cover the loaded range, not a fixed calendar day.
