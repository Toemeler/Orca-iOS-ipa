#!/bin/bash
# Build WxSmoke.app for the iOS simulator against a static wx install.
# usage: build.sh <wx-prefix> <sdk> <ios-min>
set -euxo pipefail
PREFIX="$1"; SDK="$2"; MIN="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/build"; APP="$OUT/WxSmoke.app"
mkdir -p "$APP"

SETUP_DIR=$(ls -d "$PREFIX"/lib/wx/include/*/)
WXLIBS=$(ls "$PREFIX"/lib/libwx*.a)

xcrun -sdk "$SDK" clang++ -std=c++17 -arch arm64 \
  -mios-simulator-version-min="$MIN" \
  -D__WXOSX_IPHONE__ -D_FILE_OFFSET_BITS=64 \
  -I"$PREFIX/include/wx-3.3" -I"$SETUP_DIR" \
  "$HERE/main.cpp" -o "$APP/WxSmoke" \
  $WXLIBS $WXLIBS \
  -framework UIKit -framework OpenGLES -framework QuartzCore \
  -framework CoreGraphics -framework CoreText -framework CoreFoundation \
  -framework Foundation -framework Security -framework AudioToolbox \
  -framework CFNetwork -framework MobileCoreServices \
  -lz -liconv -lexpat -llzma

cat > "$APP/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>WxSmoke</string>
  <key>CFBundleIdentifier</key><string>org.orca-ios.wxsmoke</string>
  <key>CFBundleName</key><string>WxSmoke</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>UIDeviceFamily</key><array><integer>2</integer></array>
  <key>UILaunchScreen</key><dict/>
</dict></plist>
PLIST
echo "built: $APP"
