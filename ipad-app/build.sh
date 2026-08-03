#!/bin/bash
# Build OrcaViewer.app against a static wx install.
# usage: build.sh <wx-prefix> <sdk> <ios-min>
#   <sdk> is iphonesimulator or iphoneos; the deployment-target flag and the
#   Info.plist platform keys follow from it.
set -euxo pipefail
PREFIX="$1"; SDK="$2"; MIN="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/build"; APP="$OUT/OrcaViewer.app"
rm -rf "$APP"; mkdir -p "$APP"

case "$SDK" in
  *simulator*) MIN_FLAG="-mios-simulator-version-min=$MIN"; PLATFORM="iPhoneSimulator" ;;
  *)           MIN_FLAG="-mios-version-min=$MIN";           PLATFORM="iPhoneOS" ;;
esac

SETUP_DIR=$(ls -d "$PREFIX"/lib/wx/include/*/)
WXLIBS=$(ls "$PREFIX"/lib/libwx*.a)

xcrun -sdk "$SDK" clang++ -std=c++17 -arch arm64 -O2 \
  "$MIN_FLAG" \
  -D__WXOSX_IPHONE__ -D_FILE_OFFSET_BITS=64 \
  -I"$PREFIX/include/wx-3.3" -I"$SETUP_DIR" -I"$HERE" \
  "$HERE/main.mm" -o "$APP/OrcaViewer" \
  $WXLIBS $WXLIBS \
  -framework UIKit -framework OpenGLES -framework GLKit -framework QuartzCore \
  -framework CoreGraphics -framework CoreText -framework CoreFoundation \
  -framework Foundation -framework Security -framework AudioToolbox \
  -framework CFNetwork -framework MobileCoreServices \
  -lz -liconv -lexpat -llzma

# MinimumOSVersion / CFBundleSupportedPlatforms are what installd checks on a
# real device. UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace put the
# app's folder in the Files app, which is how STLs get in.
cat > "$APP/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>OrcaViewer</string>
  <key>CFBundleIdentifier</key><string>org.orca-ios.viewer</string>
  <key>CFBundleName</key><string>OrcaViewer</string>
  <key>CFBundleDisplayName</key><string>Orca Viewer</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleSupportedPlatforms</key><array><string>${PLATFORM}</string></array>
  <key>DTPlatformName</key><string>${SDK}</string>
  <key>MinimumOSVersion</key><string>${MIN}</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
  <key>UILaunchScreen</key><dict/>
  <key>UIFileSharingEnabled</key><true/>
  <key>LSSupportsOpeningDocumentsInPlace</key><true/>
  <key>UISupportedInterfaceOrientations</key><array>
    <string>UIInterfaceOrientationPortrait</string>
    <string>UIInterfaceOrientationLandscapeLeft</string>
    <string>UIInterfaceOrientationLandscapeRight</string>
  </array>
</dict></plist>
PLIST
echo "built: $APP"
