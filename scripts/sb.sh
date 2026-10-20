#!/usr/bin/env bash
# SmartBuilding task runner for Linux (Ubuntu 22.04 / 24.04).
#
#   scripts/sb.sh check                    verify compilers, CMake, Qt, Qt MQTT and the broker
#   scripts/sb.sh install                  install missing tools (apt, Qt via aqt, Qt MQTT from source)
#   scripts/sb.sh build                    configure and build the apps and unit tests
#   scripts/sb.sh build-tests              build only the Qt-free unit tests (no Qt needed)
#   scripts/sb.sh test                     run the unit tests with ctest
#   scripts/sb.sh launch <app>             run client | server | simulator | all
#   scripts/sb.sh build-test-launch <app>  build, run unit tests, then launch the app
#   scripts/sb.sh clean                    delete the build folders
#   scripts/sb.sh broker-start|broker-stop|broker-status
#
# Optional overrides: SB_CXX (default g++-14), SB_QT_PREFIX (default ~/Qt/6.8.0/gcc_64),
#                     SB_BROKER (localhost), SB_PORT (1883), SB_DB (smartbuilding.db)
set -euo pipefail
cd "$(dirname "$0")/.."

CXX_BIN="${SB_CXX:-g++-14}"
QT_PREFIX="${SB_QT_PREFIX:-$HOME/Qt/6.8.0/gcc_64}"
DEPS_DIR="deps"
DEPS_INSTALL="$PWD/$DEPS_DIR/install"
BROKER="${SB_BROKER:-localhost}"
PORT="${SB_PORT:-1883}"
DB="${SB_DB:-smartbuilding.db}"
BUILD_DIR="build"
TESTS_DIR="build-tests"
JOBS="$(nproc)"

RED='\033[31m'; GREEN='\033[32m'; YELLOW='\033[33m'; RESET='\033[0m'
FAILED=0
ok()   { printf "${GREEN}[ OK ]${RESET} %s\n" "$*"; }
warn() { printf "${YELLOW}[WARN]${RESET} %s\n" "$*"; }
miss() { printf "${RED}[MISS]${RESET} %s\n" "$*"; FAILED=1; }

# version_ge <actual> <required>: succeeds when actual >= required
version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }

cmd_check() {
    FAILED=0
    echo "== SmartBuilding environment check (Linux) =="

    if command -v "$CXX_BIN" >/dev/null 2>&1; then
        local v
        v="$("$CXX_BIN" -dumpfullversion)"
        if [ "${v%%.*}" -ge 14 ]; then ok "$CXX_BIN $v (C++23 needs GCC 14+)"
        else miss "$CXX_BIN $v is too old (need GCC 14+). Run: install"; fi
    else
        miss "$CXX_BIN not found. Run: install"
    fi

    if command -v cmake >/dev/null 2>&1; then
        local v
        v="$(cmake --version 2>/dev/null | awk 'NR==1 {print $3}')"
        if version_ge "$v" 3.21; then ok "cmake $v"; else miss "cmake $v is too old (need 3.21+)"; fi
    else
        miss "cmake not found. Run: install"
    fi

    command -v ninja >/dev/null 2>&1 && ok "ninja (optional)" || warn "ninja not found (optional)"
    command -v git >/dev/null 2>&1 && ok "git" || miss "git not found (needed by install). Run: install"

    if [ -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        local v="unknown"
        if [ -x "$QT_PREFIX/bin/qmake" ]; then v="$("$QT_PREFIX/bin/qmake" -query QT_VERSION)"; fi
        if version_ge "$v" 6.5; then ok "Qt $v at $QT_PREFIX"; else miss "Qt $v is too old or unknown (need 6.5+)"; fi
        if qt_mqtt_installed; then ok "Qt MQTT module (deps/install)"
        else miss "Qt MQTT module not found. Run: install"; fi
    else
        miss "Qt not found at $QT_PREFIX. Run: install"
    fi

    if command -v mosquitto >/dev/null 2>&1; then ok "mosquitto broker"
    else warn "mosquitto not installed (needed to run the apps). Run: install"; fi

    if timeout 1 bash -c "</dev/tcp/$BROKER/$PORT" 2>/dev/null; then
        ok "broker listening on $BROKER:$PORT"
    else
        warn "no broker on $BROKER:$PORT (run: broker-start)"
    fi

    echo
    if [ "$FAILED" -ne 0 ]; then
        echo "Some required items are missing. Run the 'SB: Install Dependencies' task."
        return 1
    fi
    echo "Environment OK."
}

cmd_install() {
    echo "== Installing dependencies (Linux) =="
    sudo apt update
    sudo apt install -y build-essential cmake ninja-build git pipx software-properties-common \
        mosquitto mosquitto-clients sqlite3 gdb

    if ! command -v "$CXX_BIN" >/dev/null 2>&1; then
        if ! apt-cache show g++-14 >/dev/null 2>&1; then
            echo "g++-14 is not in apt on this release; adding the toolchain PPA."
            sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
            sudo apt update
        fi
        sudo apt install -y gcc-14 g++-14
    fi

    if [ ! -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        echo "Installing Qt 6.8 (several GB, this takes a while)..."
        # QT_PREFIX is <root>/6.8.0/gcc_64, so the install root is two levels up.
        local qt_root
        qt_root="$(dirname "$(dirname "$QT_PREFIX")")"
        pipx run --spec aqtinstall aqt install-qt linux desktop 6.8.0 linux_gcc_64 -O "$qt_root"
    fi

    if ! qt_mqtt_installed; then
        build_qtmqtt
    fi

    echo
    echo "Install finished. Run: scripts/sb.sh check"
}

qt_mqtt_installed() {
    [ -f "$DEPS_INSTALL/lib/cmake/Qt6Mqtt/Qt6MqttConfig.cmake" ] || \
    [ -f "$QT_PREFIX/lib/cmake/Qt6Mqtt/Qt6MqttConfig.cmake" ]
}

# Qt MQTT is built from source into deps/install, at the tag pinned in deps/qtmqtt.env.
build_qtmqtt() {
    # shellcheck source=/dev/null
    . "$DEPS_DIR/qtmqtt.env"
    local src="$DEPS_DIR/qtmqtt" build="$DEPS_DIR/build/qtmqtt"
    local qtver
    qtver="$("$QT_PREFIX/bin/qmake" -query QT_VERSION)"
    if [ "$QTMQTT_TAG" != "v$qtver" ]; then
        warn "qtmqtt $QTMQTT_TAG is pinned but Qt is $qtver; adjust deps/qtmqtt.env if the build fails"
    fi
    echo "Building Qt MQTT $QTMQTT_TAG into $DEPS_INSTALL..."
    if [ ! -d "$src/.git" ]; then
        mkdir -p "$DEPS_DIR"
        git clone "$QTMQTT_REPO" "$src"
    fi
    git -C "$src" fetch --tags
    git -C "$src" checkout --quiet "$QTMQTT_TAG"
    rm -rf "$build"
    cmake -S "$src" -B "$build" -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
          -DCMAKE_INSTALL_PREFIX="$DEPS_INSTALL" -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
    cmake --build "$build" -j"$JOBS"
    cmake --install "$build"
}

require_qt() {
    if [ ! -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ] || \
       ! qt_mqtt_installed; then
        echo "Qt 6.5+ with the MQTT module was not found at $QT_PREFIX. Run: install" >&2
        exit 1
    fi
}

cmd_build() {
    require_qt
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER="$CXX_BIN" \
          -DCMAKE_PREFIX_PATH="$QT_PREFIX;$DEPS_INSTALL" -DSB_BUILD_APPS=ON -DSB_BUILD_TESTS=ON
    cmake --build "$BUILD_DIR" -j"$JOBS"
}

cmd_build_tests() {
    cmake -S . -B "$TESTS_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER="$CXX_BIN" -DSB_BUILD_APPS=OFF
    cmake --build "$TESTS_DIR" -j"$JOBS"
}

cmd_test() {
    local dir="$BUILD_DIR"
    if [ ! -f "$dir/CTestTestfile.cmake" ]; then dir="$TESTS_DIR"; fi
    if [ ! -f "$dir/CTestTestfile.cmake" ]; then
        echo "Nothing built yet. Run: build (or build-tests)" >&2
        exit 1
    fi
    ctest --test-dir "$dir" --output-on-failure
}

cmd_launch() {
    local app="${1:-client}"
    local bin="./$BUILD_DIR/sb_$app"
    case "$app" in
        client|simulator)
            [ -x "$bin" ] || { echo "sb_$app is not built. Run: build" >&2; exit 1; }
            exec "$bin" --broker "$BROKER" --port "$PORT" ;;
        server)
            bin="./$BUILD_DIR/sb_server"
            [ -x "$bin" ] || { echo "sb_server is not built. Run: build" >&2; exit 1; }
            exec "$bin" --broker "$BROKER" --port "$PORT" --database "$DB" ;;
        all)
            BROKER="$BROKER" PORT="$PORT" DB="$DB" exec bash scripts/launch-all.sh ;;
        *)
            echo "Unknown app '$app' (use client, server, simulator or all)" >&2
            exit 1 ;;
    esac
}

cmd_build_test_launch() {
    local app="${1:-client}"
    cmd_build
    cmd_test
    cmd_launch "$app"
}

cmd_clean() {
    rm -rf "$BUILD_DIR" "$TESTS_DIR"
    echo "Removed $BUILD_DIR and $TESTS_DIR"
}

cmd_broker_status() {
    systemctl status mosquitto --no-pager || true
    ss -ltn | grep ":$PORT" || echo "nothing listening on port $PORT"
}

usage() {
    cat <<USAGE
Usage: scripts/sb.sh <command> [app]

  check                    verify the environment
  install                  install missing dependencies
  build                    build apps and unit tests
  build-tests              build the unit tests only (no Qt)
  test                     run the unit tests
  launch <app>             client | server | simulator | all
  build-test-launch <app>  build, test, then launch
  clean                    remove build folders
  broker-start | broker-stop | broker-status
USAGE
}

cmd="${1:-help}"
shift || true
case "$cmd" in
    check)              cmd_check ;;
    install)            cmd_install ;;
    build)              cmd_build ;;
    build-tests)        cmd_build_tests ;;
    test)               cmd_test ;;
    launch)             cmd_launch "${1:-client}" ;;
    build-test-launch)  cmd_build_test_launch "${1:-client}" ;;
    clean)              cmd_clean ;;
    broker-start)       sudo systemctl start mosquitto ;;
    broker-stop)        sudo systemctl stop mosquitto ;;
    broker-status)      cmd_broker_status ;;
    help|-h|--help)     usage ;;
    *)                  echo "Unknown command: $cmd" >&2; usage; exit 1 ;;
esac
