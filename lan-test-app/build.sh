#!/usr/bin/env bash
# Builds BambuLAN.app - the LAN backend test app.
#
#   lan-test-app/build.sh <openssl-prefix> <sdk> <ios-min> [curl-prefix] [json-include-dir]
#
# <sdk> is iphoneos or iphonesimulator. When a curl prefix is given the app is
# built with the FTPS upload/print buttons; without one it is MQTT only, which
# still covers connect, motion, extrusion, temperatures, fans, light and job
# control.
set -euo pipefail

SSL_PREFIX="${1:?openssl prefix required}"
SDK="${2:-iphoneos}"
IOS_MIN="${3:-17.0}"
CURL_PREFIX="${4:-}"
JSON_INC="${5:-}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
SRC="$REPO/orca-overlay/src/slic3r/Utils"
OUT="$HERE/build"
APP="$OUT/BambuLAN.app"

if [ -z "$JSON_INC" ]; then
    for candidate in "$HERE/third_party" "$REPO/../orca/deps_src" /tmp/orca/deps_src; do
        [ -f "$candidate/nlohmann/json.hpp" ] && JSON_INC="$candidate" && break
    done
fi
[ -f "$JSON_INC/nlohmann/json.hpp" ] || { echo "error: nlohmann/json.hpp not found (arg 5)"; exit 1; }

SDK_PATH="$(xcrun --sdk "$SDK" --show-sdk-path)"
if [ "$SDK" = "iphonesimulator" ]; then
    MIN_FLAG="-mios-simulator-version-min=$IOS_MIN"
else
    MIN_FLAG="-miphoneos-version-min=$IOS_MIN"
fi

CURL_FLAGS=()
CURL_LIBS=()
if [ -n "$CURL_PREFIX" ] && [ -f "$CURL_PREFIX/lib/libcurl.a" ]; then
    echo "building with FTPS support from $CURL_PREFIX"
    CURL_FLAGS=(-DBAMBU_LAN_WITH_FTPS -I"$CURL_PREFIX/include")
    CURL_LIBS=("$CURL_PREFIX/lib/libcurl.a" -lz -framework SystemConfiguration)
else
    echo "building WITHOUT FTPS support (no libcurl at '${CURL_PREFIX:-}')"
fi

rm -rf "$OUT"
mkdir -p "$APP"

# -x objective-c++ stays in force for every argument that follows it, static
# libraries included - clang would try to *compile* libssl.a. -x none puts the
# language back to "infer from the file" before the link inputs.
set -x
xcrun --sdk "$SDK" clang++ \
    -x objective-c++ -std=c++17 -fobjc-arc -O1 -g \
    -isysroot "$SDK_PATH" -arch arm64 "$MIN_FLAG" \
    -I"$SRC" -I"$JSON_INC" -I"$SSL_PREFIX/include" "${CURL_FLAGS[@]}" \
    "$HERE/main.mm" \
    "$SRC/BambuLanMqtt.cpp" \
    "$SRC/BambuLanFtps.cpp" \
    "$SRC/BambuLanDiscovery.cpp" \
    "$SRC/BambuLanPrintCommand.cpp" \
    -x none \
    "$SSL_PREFIX/lib/libssl.a" "$SSL_PREFIX/lib/libcrypto.a" "${CURL_LIBS[@]}" \
    -framework UIKit -framework Foundation -framework CoreGraphics \
    -framework UniformTypeIdentifiers -framework Security \
    -o "$APP/BambuLAN"
set +x

cat > "$APP/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>BambuLAN</string>
  <key>CFBundleIdentifier</key><string>org.orca-ios.bambulan</string>
  <key>CFBundleName</key><string>BambuLAN</string>
  <key>CFBundleDisplayName</key><string>BambuLAN</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>MinimumOSVersion</key><string>${IOS_MIN}</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
  <key>UILaunchScreen</key><dict/>
  <key>UISupportedInterfaceOrientations</key><array>
    <string>UIInterfaceOrientationLandscapeLeft</string>
    <string>UIInterfaceOrientationLandscapeRight</string>
    <string>UIInterfaceOrientationPortrait</string>
  </array>
  <key>UIFileSharingEnabled</key><true/>
  <key>LSSupportsOpeningDocumentsInPlace</key><true/>
  <!-- iOS 14+ blocks every local-network connection until the user agrees.
       Without this key the connection to the printer fails silently. -->
  <key>NSLocalNetworkUsageDescription</key>
  <string>BambuLAN talks to your 3D printer on this network over MQTT and FTPS.</string>
  <key>NSBonjourServices</key><array>
    <string>_bambulab._tcp</string>
  </array>
  <key>NSAppTransportSecurity</key><dict>
    <key>NSAllowsLocalNetworking</key><true/>
  </dict>
</dict></plist>
PLIST

echo "built $APP"
ls -l "$APP"
