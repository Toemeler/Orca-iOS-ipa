#!/usr/bin/env bash
# Install an iOS .app in a fresh iPad simulator, launch it, then walk the UI:
# tap each place named in the step list, screenshotting after every one.
#
# This exists so the app can be exercised page by page without a person and a
# sideload in the loop. It deliberately does NOT build anything - it takes a
# bundle that ios-step3-gui.yml already built and published, so a round trip is
# minutes rather than the ~75 the full Orca compile costs.
#
# usage: sim-drive.sh <path to .app> <bundle id> <output dir>
#
# Steps come from DRIVE_STEPS: "label:x:y" separated by spaces, coordinates in
# POINTS (idb's unit), not pixels. The iPad Pro 13" M4 is 1032x1376 points at
# 2x, so a coordinate read off a 2064x2752 screenshot must be halved.
#
# Every tap is best effort. If idb is unavailable or a tap fails, the run still
# produces launch screenshots and logs - degraded, not dead, because the launch
# evidence is worth having even when the driving is not working yet.

set -uo pipefail

APP="${1:?usage: sim-drive.sh <app> <bundle-id> <outdir>}"
BUNDLE_ID="${2:?missing bundle id}"
OUT="${3:?missing output dir}"

# Default walk: the four tabs across the top of Orca's main frame, then back to
# home. Read off ci-logs/step3-run-65/ui-t67s.png.
DRIVE_STEPS="${DRIVE_STEPS:-prepare:122:50 preview:257:50 device:393:50 project:530:50 home:20:50}"
SETTLE="${DRIVE_SETTLE:-4}"

mkdir -p "$OUT"
LOG="$OUT/drive-log.txt"
: > "$LOG"
note() { echo "$@" | tee -a "$LOG"; }

RT=$(xcrun simctl list runtimes | grep -o 'com.apple.CoreSimulator.SimRuntime.iOS-[0-9-]*' | tail -1)
UDID=$(xcrun simctl create "orca-drive" \
  "com.apple.CoreSimulator.SimDeviceType.iPad-Pro-13-inch-M4-16GB" "$RT")
note "simulator: $UDID ($RT)"
xcrun simctl boot "$UDID"
xcrun simctl bootstatus "$UDID" -b

xcrun simctl install "$UDID" "$APP" 2>&1 | tee -a "$LOG"

# Reaching the Prepare page needs no tap at all. GUI_App::on_init_inner hands
# init_params->input_files to plater()->load_files(), so an ordinary positional
# argument loads a model and puts the 3D editor up - which is exactly the page
# the black-viewport question is about. simctl passes trailing arguments
# straight to the app, and a simulator app's paths are host paths, so the file
# can simply be copied into its container first.
MODEL="${DRIVE_MODEL:-}"
if [ -n "$MODEL" ] && [ -f "$MODEL" ]; then
  DATA=$(xcrun simctl get_app_container "$UDID" "$BUNDLE_ID" data 2>/dev/null)
  if [ -n "${DATA:-}" ]; then
    mkdir -p "$DATA/Documents"
    cp "$MODEL" "$DATA/Documents/" && MODEL_ARG="$DATA/Documents/$(basename "$MODEL")"
    note "launching with model: $MODEL_ARG"
  fi
fi

# One launch, with a console attached: the app's own stdout/stderr is where
# UIKit's EAGL complaints land, and those are the whole reason this exists.
#
# It is deliberately not launched twice. An earlier version did a plain launch
# for the pid and exit status and then a --console-pty launch for the output,
# but --terminate-running-process on the second one kills the first instance,
# so the run was measuring an app that had just been shot. Whether the launch
# itself succeeded is now read from the pid line simctl prints into the log.
# shellcheck disable=SC2086
xcrun simctl launch --console-pty --terminate-running-process "$UDID" "$BUNDLE_ID" ${MODEL_ARG:-} \
  > "$OUT/sim-launch.log" 2>&1 &
LPID=$!

EXE=$(basename "$APP" .app)
alive() {
  pgrep -f "\.app/${EXE}" >/dev/null 2>&1 && return 0
  pgrep -x "$EXE" >/dev/null 2>&1 && return 0
  return 1
}

FIRST_ALIVE=""
for i in $(seq 1 60); do
  if alive; then FIRST_ALIVE="$i"; break; fi
  sleep 1
done
note "first seen alive after: ${FIRST_ALIVE:-never} s"

# Orca does a lot before its first frame; do not start tapping into a window
# that has not been drawn yet.
sleep "${DRIVE_WARMUP:-20}"
xcrun simctl io "$UDID" screenshot "$OUT/drive-00-start.png" >/dev/null 2>&1

# idb is the only supported way to synthesise a tap into a simulator without an
# XCUITest target. It is optional here on purpose: when it is missing the run
# still reports launch and startup evidence instead of failing outright.
IDB=""
command -v idb >/dev/null 2>&1 && IDB=idb
if [ -z "$IDB" ]; then
  note "idb not available - launch evidence only, no UI walk"
else
  idb connect "$UDID" >>"$LOG" 2>&1 || note "idb connect failed (continuing)"
fi

N=0
for step in $DRIVE_STEPS; do
  LABEL="${step%%:*}"; REST="${step#*:}"; X="${REST%%:*}"; Y="${REST##*:}"
  N=$((N + 1))
  IDX=$(printf '%02d' "$N")
  if ! alive; then
    note "step $IDX $LABEL: app is GONE before this tap - stopping the walk"
    break
  fi
  if [ -n "$IDB" ]; then
    note "step $IDX $LABEL: tap ($X,$Y)"
    idb ui tap --udid "$UDID" "$X" "$Y" >>"$LOG" 2>&1 \
      || note "step $IDX $LABEL: tap FAILED"
  else
    note "step $IDX $LABEL: (skipped, no idb)"
  fi
  sleep "$SETTLE"
  xcrun simctl io "$UDID" screenshot "$OUT/drive-$IDX-$LABEL.png" >/dev/null 2>&1 \
    || note "step $IDX $LABEL: screenshot failed"
  alive || note "step $IDX $LABEL: app DIED during this step"
done

STILL_ALIVE=0; alive && STILL_ALIVE=1
note "still alive after the walk: $STILL_ALIVE"

kill "$LPID" 2>/dev/null || true

# Same evidence set the launch verifier collects: the app's own log, the
# startup-error note 0338 writes, and the crash report from wherever the
# simulator decided to put it.
DATA=$(xcrun simctl get_app_container "$UDID" "$BUNDLE_ID" data 2>/dev/null)
if [ -n "${DATA:-}" ]; then
  find "$DATA" -name "*.log" -o -name "*.log.[0-9]*" \
    | while IFS= read -r f; do echo "--- $f ---"; tail -c 40000 "$f"; done \
    > "$OUT/orca-app-log.txt" 2>&1 || true

  # The tail is the wrong end of the file for anything about startup. Orca
  # writes tens of thousands of preset lines, so OpenGL init - which happens
  # first and says whether the context, GLAD and the shaders came up - is
  # always past the 40000-byte cut. Pull those lines out of the whole log.
  find "$DATA" -name "*.log" -o -name "*.log.[0-9]*" \
    | while IFS= read -r f; do
        echo "--- $f ---"
        grep -inE "opengl|glad|shader|framebuffer|glcanvas|canvas3d|es 3|renderer|gl version|Unable to|Error loading" "$f"
      done > "$OUT/orca-gl-log.txt" 2>&1 || true
  echo "=== GL lines from the app's own log ==="
  head -60 "$OUT/orca-gl-log.txt"

  # Every top level window that appeared or disappeared, and every modal loop
  # that ran, from the wx instrumentation in patches/step2/0215 and 0216. This
  # is what answers "the wizard never came up" / "the dropdown does nothing"
  # without anyone having to look at a screenshot: a dialog that was never
  # created logs nothing, one that was created and left behind the main window
  # logs a SHOW with level=0, and one that was shown and torn down in the same
  # millisecond logs a SHOW and a HIDE with no modal loop between them.
  find "$DATA" -name "*.log" -o -name "*.log.[0-9]*" \
    | while IFS= read -r f; do
        echo "--- $f ---"
        grep -inE "wx-ios|modal loop|ConfigWizard|GuideFrame|LoginDialog|PreferencesDialog|PhysicalPrinterDialog|DropDown|Popup" "$f"
      done > "$OUT/orca-ui-log.txt" 2>&1 || true
  echo "=== window / dialog lines from the app's own log ==="
  head -80 "$OUT/orca-ui-log.txt"

  # And the whole log, compressed. Reading the last 40000 bytes of a file whose
  # first megabyte is the interesting part has cost this port several rounds.
  find "$DATA" -name "*.log" -o -name "*.log.[0-9]*" \
    | while IFS= read -r f; do echo "--- $f ---"; cat "$f"; done \
    | gzip -9 > "$OUT/orca-app-log-full.txt.gz" 2>/dev/null || true
  [ -f "$DATA/Documents/orca-startup-error.txt" ] && \
    cp "$DATA/Documents/orca-startup-error.txt" "$OUT/" 2>/dev/null
fi
for d in "$HOME/Library/Logs/DiagnosticReports" \
         "$HOME/Library/Logs/CoreSimulator/$UDID" \
         "$HOME/Library/Developer/CoreSimulator/Devices/$UDID/data/Library/Logs/CrashReporter"; do
  [ -d "$d" ] || continue
  find "$d" \( -name "${EXE}*.ips" -o -name "${EXE}*.crash" \) -exec cp {} "$OUT/" \; 2>/dev/null
done
# `process == "OrcaSlicer"` matches nothing when the app dies before the log
# subsystem ever attributes a message to it - run 66's capture was 133 bytes of
# header. Ask a much wider question: anything mentioning the bundle id or the
# executable, from any process, including SpringBoard and launchd, which are
# the ones that say why a launch was refused or a process was killed.
xcrun simctl spawn "$UDID" log show --last 10m \
  --predicate "process == \"$EXE\" OR eventMessage CONTAINS \"$EXE\" OR eventMessage CONTAINS \"$BUNDLE_ID\" OR processImagePath CONTAINS \"$EXE\"" \
  > "$OUT/sim-oslog.txt" 2>&1 || true
xcrun simctl spawn "$UDID" log show --last 10m --predicate \
  'senderImagePath CONTAINS "SpringBoard" OR process == "launchd" OR process == "runningboardd"' \
  > "$OUT/sim-oslog-system.txt" 2>&1 || true

xcrun simctl shutdown "$UDID" >/dev/null 2>&1 || true
xcrun simctl delete "$UDID" >/dev/null 2>&1 || true

note "screenshots: $(ls -1 "$OUT"/drive-*.png 2>/dev/null | wc -l | tr -d ' ')"
[ -n "$FIRST_ALIVE" ] || { echo "::error::$BUNDLE_ID never started"; exit 1; }
exit 0
