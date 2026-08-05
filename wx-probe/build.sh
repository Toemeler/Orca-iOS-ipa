#!/bin/bash
# Build WxProbe.app against the cached static wx iOS install.
# usage: build.sh <wx-prefix> <sdk> <ios-min> [deps-prefix]
#
# Same link set as smoke-app/build.sh (which is the one proven in step 2) plus
# the deps prefix if one is given, since a richer widget set can pull in the
# image handlers wx was built against.
set -euxo pipefail
PREFIX="$1"; SDK="$2"; MIN="$3"; DEPS="${4:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/build"; APP="$OUT/WxProbe.app"
rm -rf "$OUT"; mkdir -p "$APP"

case "$SDK" in
  *simulator*) MIN_FLAG="-mios-simulator-version-min=$MIN"; PLATFORM="iPhoneSimulator" ;;
  *)           MIN_FLAG="-mios-version-min=$MIN";           PLATFORM="iPhoneOS" ;;
esac

SETUP_DIR=$(ls -d "$PREFIX"/lib/wx/include/*/)
WXLIBS=$(ls "$PREFIX"/lib/libwx*.a)
DEPSLIBS=""
[ -n "$DEPS" ] && [ -d "$DEPS/usr/local/lib" ] && DEPSLIBS="-L$DEPS/usr/local/lib"

xcrun -sdk "$SDK" clang++ -std=c++17 -arch arm64 \
  "$MIN_FLAG" \
  -D__WXOSX_IPHONE__ -D_FILE_OFFSET_BITS=64 \
  -I"$PREFIX/include/wx-3.3" -I"$SETUP_DIR" \
  "$HERE/main.cpp" -o "$APP/WxProbe" \
  $DEPSLIBS \
  $WXLIBS $WXLIBS \
  -framework UIKit -framework OpenGLES -framework GLKit -framework QuartzCore \
  -framework CoreGraphics -framework CoreText -framework CoreFoundation \
  -framework Foundation -framework Security -framework AudioToolbox \
  -framework CFNetwork -framework MobileCoreServices \
  -framework UniformTypeIdentifiers \
  -lz -liconv -lexpat -llzma

# A .dSYM inside the bundle makes ldid assert on filetype (MH_DSYM is not one of
# the four it accepts), and there is no reason for one here.
rm -rf "$APP"/*.dSYM

cat > "$APP/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>WxProbe</string>
  <key>CFBundleIdentifier</key><string>org.orca-ios.wxprobe</string>
  <key>CFBundleName</key><string>WxProbe</string>
  <key>CFBundleDisplayName</key><string>wx probe</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleSupportedPlatforms</key><array><string>${PLATFORM}</string></array>
  <key>DTPlatformName</key><string>${SDK}</string>
  <key>MinimumOSVersion</key><string>${MIN}</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>UIDeviceFamily</key><array><integer>2</integer></array>
  <key>UILaunchScreen</key><dict/>
  <key>UIRequiresFullScreen</key><true/>
  <key>UISupportedInterfaceOrientations</key><array>
    <string>UIInterfaceOrientationLandscapeLeft</string>
    <string>UIInterfaceOrientationLandscapeRight</string>
    <string>UIInterfaceOrientationPortrait</string>
  </array>
</dict></plist>
PLIST

# Nothing but the executable and the plist: a top-level resources/Resources
# directory would make CFBundle read this as an old-style bundle and the
# installer would report "Missing bundle ID".
find "$APP" -mindepth 1 -maxdepth 1 | sort
echo "built: $APP"
