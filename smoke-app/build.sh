#!/bin/bash
# Build WxSmoke.app against a static wx install.
# usage: build.sh <wx-prefix> <sdk> <ios-min>
#   <sdk> is either iphonesimulator or iphoneos; the deployment-target flag and
#   the Info.plist platform keys follow from it, so the same script produces the
#   simulator proof (step 2) and the sideloadable device bundle (device IPA).
set -euxo pipefail
PREFIX="$1"; SDK="$2"; MIN="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/build"; APP="$OUT/WxSmoke.app"
mkdir -p "$APP"

case "$SDK" in
  *simulator*) MIN_FLAG="-mios-simulator-version-min=$MIN"; PLATFORM="iPhoneSimulator" ;;
  *)           MIN_FLAG="-mios-version-min=$MIN";           PLATFORM="iPhoneOS" ;;
esac

SETUP_DIR=$(ls -d "$PREFIX"/lib/wx/include/*/)
WXLIBS=$(ls "$PREFIX"/lib/libwx*.a)

xcrun -sdk "$SDK" clang++ -std=c++17 -arch arm64 \
  "$MIN_FLAG" \
  -D__WXOSX_IPHONE__ -D_FILE_OFFSET_BITS=64 \
  -I"$PREFIX/include/wx-3.3" -I"$SETUP_DIR" \
  "$HERE/main.cpp" "$HERE/../wx-probe/probe_boot.mm" -o "$APP/WxSmoke" \
  $WXLIBS $WXLIBS \
  -framework UIKit -framework OpenGLES -framework GLKit -framework QuartzCore \
  -framework CoreGraphics -framework CoreText -framework CoreFoundation \
  -framework Foundation -framework Security -framework AudioToolbox \
  -framework CFNetwork -framework MobileCoreServices \
  -lz -liconv -lexpat -llzma

# UIApplicationMain names wx's delegate as the string @"wxAppDelegate", so no
# undefined symbol points at it. Report whether the linker kept it: an app that
# lost it runs with a nil delegate and can never reach OnInit.
if nm -a "$APP/WxSmoke" 2>/dev/null | grep -q '_OBJC_CLASS_\$_wxAppDelegate'; then
  echo "SMOKE_DELEGATE=linked"
else
  echo "SMOKE_DELEGATE=MISSING - wxAppDelegate is not in the binary"
fi

# MinimumOSVersion / CFBundleSupportedPlatforms are what installd checks on a
# real device; without them the IPA installs nowhere. Harmless in the simulator.
cat > "$APP/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>WxSmoke</string>
  <key>CFBundleIdentifier</key><string>org.orca-ios.wxsmoke</string>
  <key>CFBundleName</key><string>WxSmoke</string>
  <key>CFBundleDisplayName</key><string>Orca Smoke</string>
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
