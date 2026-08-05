#!/usr/bin/env bash
# Assemble a flat iOS OrcaSlicer.app from a linked binary and a resources tree.
#
# usage: assemble-sim-bundle.sh <binary> <resources dir> <out .app> [ios-min]
#
# Shared by the workflow that builds Orca and the one that only relaunches the
# last build, so the bundle under test cannot drift between them.
set -euo pipefail

BIN="${1:?usage: assemble-sim-bundle.sh <binary> <resources> <out.app> [min]}"
RES="${2:?missing resources dir}"
APP="${3:?missing output .app path}"
MIN="${4:-17.0}"

rm -rf "$APP"; mkdir -p "$APP"
cp "$BIN" "$APP/OrcaSlicer"
chmod +x "$APP/OrcaSlicer"

# Info.plist first, resources second. simctl reads the plist to get the bundle
# id, and resources is the biggest thing on the disk - if space runs out
# mid-copy, a plist written afterwards ends up empty and the install fails with
# the useless "Missing bundle ID".
cat > "$APP/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>OrcaSlicer</string>
  <key>CFBundleIdentifier</key><string>org.orca-ios.orcaslicer</string>
  <key>CFBundleName</key><string>OrcaSlicer</string>
  <key>CFBundleDisplayName</key><string>OrcaSlicer</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>MinimumOSVersion</key><string>${MIN}</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>UIDeviceFamily</key><array><integer>2</integer></array>
  <key>UILaunchScreen</key><dict/>
  <!-- iOS 14+ blocks every local-network connection until the user agrees.
       Without this key the native Bambu LAN backend cannot reach the printer at
       all, and the failure is silent. -->
  <key>NSLocalNetworkUsageDescription</key>
  <string>OrcaSlicer talks to 3D printers on your local network.</string>
  <key>NSBonjourServices</key><array><string>_bambulab._tcp</string></array>
  <key>NSAppTransportSecurity</key><dict>
    <key>NSAllowsLocalNetworking</key><true/>
  </dict>
  <key>UIRequiresFullScreen</key><true/>
  <key>UISupportedInterfaceOrientations</key><array>
    <string>UIInterfaceOrientationLandscapeLeft</string>
    <string>UIInterfaceOrientationLandscapeRight</string>
    <string>UIInterfaceOrientationPortrait</string>
  </array>
</dict></plist>
PLIST

cp -R "$RES" "$APP/orca-resources"

# A top-level "resources" directory is "Resources" on a case-insensitive
# filesystem, and CFBundle then reads the flat bundle as an old-style one whose
# Info.plist lives inside it - the installer reports the useless "Missing bundle
# ID". Proved by .github/workflows/ios-sim-probe.yml round 3.
if [ -e "$APP/resources" ] || [ -e "$APP/Resources" ]; then
  echo "::error::$APP has a top-level resources directory; installers will reject it"
  exit 1
fi
# A .dSYM anywhere in the bundle makes ldid assert on filetype.
rm -rf "$APP"/*.dSYM

python3 - "$APP/Info.plist" <<'VERIFY'
import plistlib, sys
raw = open(sys.argv[1], 'rb').read()
if not raw:
    sys.exit('Info.plist is EMPTY (disk full while writing?)')
d = plistlib.loads(raw)
missing = [k for k in ('CFBundleIdentifier', 'CFBundleExecutable') if not d.get(k)]
if missing:
    sys.exit('Info.plist lacks %s' % missing)
print('Info.plist ok: %s / %s' % (d['CFBundleIdentifier'], d['CFBundleExecutable']))
VERIFY

test -x "$APP/OrcaSlicer" || { echo "::error::$APP/OrcaSlicer missing or not executable"; exit 1; }
echo "assembled $APP ($(du -sh "$APP" | cut -f1), $(find "$APP" -type f | wc -l | tr -d ' ') files)"
