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

# probe_boot.mm is instrumentation, not the experiment. If it fails to compile
# it must not take the launch run down with it -- that already cost two rounds,
# and the delegate check below does not depend on it at all.
BOOT_SRC="$HERE/probe_boot.mm"

build_it() {
  xcrun -sdk "$SDK" clang++ -std=c++17 -arch arm64 \
    "$MIN_FLAG" \
    -D__WXOSX_IPHONE__ -D_FILE_OFFSET_BITS=64 "$@" \
    -I"$PREFIX/include/wx-3.3" -I"$SETUP_DIR" \
    "$HERE/main.cpp" $BOOT_SRC -o "$APP/WxProbe" \
    $DEPSLIBS \
    $WXLIBS $WXLIBS \
    -framework UIKit -framework OpenGLES -framework GLKit -framework QuartzCore \
    -framework CoreGraphics -framework CoreText -framework CoreFoundation \
    -framework Foundation -framework Security -framework AudioToolbox \
    -framework CFNetwork -framework MobileCoreServices \
    -framework UniformTypeIdentifiers \
    -lz -liconv -lexpat -llzma
}

# The WebView tab is the one that might not build: wx's WKWebView backend was
# only compiled for the iPhone port by step-2 patch 0210, and if that is not in
# this cached prefix the include is not there. Losing the whole probe to it
# would waste the run, so try with it and fall back without - and say which.
# Plain string, not an array: the runner's /bin/bash is 3.2, where expanding an
# empty array under `set -u` is itself a fatal "unbound variable" error.
EXTRA_FLAGS=""
if build_it -DPROBE_WEBVIEW -framework WebKit; then
  echo "PROBE_BUILD=with-webview"
  EXTRA_FLAGS="-DPROBE_WEBVIEW -framework WebKit"
elif build_it; then
  echo "PROBE_BUILD=without-webview (the WebKit/wxWebView build failed above)"
else
  # Drop the instrumentation and try once more. main.cpp declares
  # probe_note_oninit weak, so it links and no-ops without this file.
  echo "PROBE_BUILD=instrumentation FAILED TO COMPILE - see the errors above"
  BOOT_SRC=""
  if build_it -DPROBE_WEBVIEW -framework WebKit; then
    echo "PROBE_BUILD=with-webview, no instrumentation"
    EXTRA_FLAGS="-DPROBE_WEBVIEW -framework WebKit"
  else
    build_it
    echo "PROBE_BUILD=without-webview, no instrumentation"
  fi
fi

# Is wx's UIApplication delegate actually in the linked binary?
#
# UIApplicationMain names it as a *string* -- @"wxAppDelegate" -- so there is no
# undefined symbol pointing at it, and a static archive member that resolves
# nothing is not loaded. An app that loses it runs with a nil delegate: UIKit
# still builds a full scene stack (which is what the system log has been
# showing), no launch callback is ever delivered, and OnInit is never reached.
# That is exactly the observed failure, so check it at build time rather than
# spending another simulator run on it.
delegate_present() {
  nm -a "$APP/WxProbe" 2>/dev/null | grep -q '_OBJC_CLASS_\$_wxAppDelegate'
}

if delegate_present; then
  echo "PROBE_DELEGATE=linked (wxAppDelegate is in the binary)"
else
  echo "PROBE_DELEGATE=MISSING - wxAppDelegate was stripped; relinking with -ObjC"
  build_it $EXTRA_FLAGS -ObjC
  if delegate_present; then
    echo "PROBE_DELEGATE=linked-via-ObjC (ROOT CAUSE: the app must link wx with -ObjC)"
  else
    echo "PROBE_DELEGATE=STILL-MISSING even with -ObjC"
  fi
fi

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
