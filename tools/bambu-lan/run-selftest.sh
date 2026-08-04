#!/usr/bin/env bash
# Builds and runs the Bambu LAN backend self test against the mock printer.
#
#   tools/bambu-lan/run-selftest.sh [work-dir]
#
# Needs a host compiler, OpenSSL and libcurl development headers, python3, and
# a copy of nlohmann/json.hpp (fetched from the pinned OrcaSlicer checkout if
# one is present, otherwise from NLOHMANN_JSON_DIR).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SRC="$REPO/orca-overlay/src/slic3r/Utils"
WORK="${1:-${TMPDIR:-/tmp}/bambu-lan-selftest}"

MQTT_PORT="${MQTT_PORT:-8883}"
FTP_PORT="${FTP_PORT:-990}"
SSDP_PORT="${SSDP_PORT:-2021}"
ACCESS_CODE="${ACCESS_CODE:-12345678}"
SERIAL="${SERIAL:-00M09A351100999}"

rm -rf "$WORK"
mkdir -p "$WORK/state"

# --- nlohmann/json ----------------------------------------------------------
JSON_INC="${NLOHMANN_JSON_DIR:-}"
if [ -z "$JSON_INC" ]; then
    for candidate in "$WORK/../orca/deps_src" "$REPO/../orca/deps_src" /tmp/orca/deps_src; do
        if [ -f "$candidate/nlohmann/json.hpp" ]; then
            JSON_INC="$candidate"
            break
        fi
    done
fi
if [ -z "$JSON_INC" ] || [ ! -f "$JSON_INC/nlohmann/json.hpp" ]; then
    echo "error: nlohmann/json.hpp not found; set NLOHMANN_JSON_DIR to the directory containing nlohmann/" >&2
    exit 1
fi
echo "using nlohmann from $JSON_INC"

# --- self-signed certificate, like the printer's ----------------------------
openssl req -x509 -newkey rsa:2048 -keyout "$WORK/key.pem" -out "$WORK/cert.pem" \
    -days 2 -nodes -subj "/CN=BambuLab" >/dev/null 2>&1

# --- build ------------------------------------------------------------------
CXX="${CXX:-c++}"
CURL_CFLAGS="$(curl-config --cflags 2>/dev/null || echo)"
CURL_LIBS="$(curl-config --libs 2>/dev/null || echo -lcurl)"

set -x
$CXX -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"$SRC" -I"$JSON_INC" $CURL_CFLAGS \
    "$HERE/selftest.cpp" \
    "$SRC/BambuLanMqtt.cpp" \
    "$SRC/BambuLanFtps.cpp" \
    "$SRC/BambuLanDiscovery.cpp" \
    "$SRC/BambuLanPrintCommand.cpp" \
    -o "$WORK/selftest" \
    -lssl -lcrypto $CURL_LIBS -lpthread
set +x

# --- mock printer -----------------------------------------------------------
python3 "$HERE/mock_printer.py" \
    --mqtt-port "$MQTT_PORT" --ftp-port "$FTP_PORT" --ssdp-port "$SSDP_PORT" \
    --access-code "$ACCESS_CODE" --serial "$SERIAL" \
    --cert "$WORK/cert.pem" --key "$WORK/key.pem" \
    --state-dir "$WORK/state" &
MOCK_PID=$!
trap 'kill $MOCK_PID 2>/dev/null || true' EXIT

for _ in $(seq 1 40); do
    [ -f "$WORK/state/ready" ] && break
    sleep 0.25
done
if [ ! -f "$WORK/state/ready" ]; then
    echo "error: mock printer did not come up" >&2
    exit 1
fi

# --- run --------------------------------------------------------------------
"$WORK/selftest" \
    --host 127.0.0.1 --mqtt-port "$MQTT_PORT" --ftp-port "$FTP_PORT" --ssdp-port "$SSDP_PORT" \
    --access-code "$ACCESS_CODE" --serial "$SERIAL" --state-dir "$WORK/state"
