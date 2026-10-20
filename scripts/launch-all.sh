#!/usr/bin/env bash
# Starts the server and simulator in the background, then runs the client in the
# foreground. Closing the client stops the other two.
set -euo pipefail
cd "$(dirname "$0")/.."

BROKER="${BROKER:-localhost}"
PORT="${PORT:-1883}"
DB="${DB:-smartbuilding.db}"

pids=()
cleanup() {
    echo "Stopping background services..."
    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

./build/sb_server --broker "$BROKER" --port "$PORT" --database "$DB" &
pids+=($!)
sleep 1

./build/sb_simulator --broker "$BROKER" --port "$PORT" &
pids+=($!)
sleep 1

./build/sb_client --broker "$BROKER" --port "$PORT"
