#!/usr/bin/env bash
# Install an iOS .app in a fresh iPad simulator, launch it, and prove it is
# still running - with screenshots taken while it is alive.
#
# The point is what the old version of this got wrong. `simctl launch` returns 0
# as soon as the process is spawned, and `simctl io screenshot` will happily
# photograph the home screen, so "installed, launched, here is a picture" was
# reported for a build that exited during startup and left the simulator sitting
# on SpringBoard. Aliveness is polled from the host pid `simctl launch` prints,
# the screenshots are taken between those polls, and a build that never appears
# - or appears and then goes away - fails the step.
#
# usage: sim-launch-verify.sh <path to .app> <bundle id> <output dir>
#
# Writes into <output dir>:
#   00-home.png          the home screen before the launch, for comparison
#   ui-tNNs.png          one screenshot per checkpoint, taken while alive
#   orca-on-ipad.png     copy of the last screenshot taken while alive
#   launch-verdict.txt   aliveness timeline
#   screenshot-stats.txt colour statistics for every screenshot
#   sim-launch.log       the app's stdout/stderr
#   sim-oslog.txt        the simulator's log for the process
#   orca-app-log.txt     the app's own log, out of its data container
#
# Exit status: 0 only if the app was alive at every checkpoint.

set -uo pipefail

APP="${1:?usage: sim-launch-verify.sh <app> <bundle-id> <outdir>}"
BUNDLE_ID="${2:?missing bundle id}"
OUT="${3:?missing output dir}"

# Checkpoints in seconds after the process first appears. Orca does a lot of
# work before its first frame, so the early shots may show an empty window and
# the later ones the real UI; all of them are published.
CHECKPOINTS="${SIM_CHECKPOINTS:-2 7 17 37 67}"
STARTUP_TIMEOUT="${SIM_STARTUP_TIMEOUT:-60}"

mkdir -p "$OUT"

RT=$(xcrun simctl list runtimes | grep -o 'com.apple.CoreSimulator.SimRuntime.iOS-[0-9-]*' | tail -1)
UDID=$(xcrun simctl create "orca-ipad" \
  "com.apple.CoreSimulator.SimDeviceType.iPad-Pro-13-inch-M4-16GB" "$RT")
echo "simulator: $UDID ($RT)"
# Which runtime this ran against, published rather than left in the job log.
# The selection is "newest installed", so a runner image update can move it
# without a single line of this repo changing - and an app that stops launching
# after such a move looks exactly like a code regression. iOS 26 makes UIScene
# adoption mandatory, which a wx app built on the legacy
# applicationDidFinishLaunching: path does not do, so the runtime version is
# load-bearing evidence and belongs next to the verdict.
SIM_RUNTIME="$RT"
xcrun simctl list runtimes > "$OUT/sim-runtimes.txt" 2>&1 || true
[ -n "${GITHUB_ENV:-}" ] && echo "UDID=$UDID" >> "$GITHUB_ENV"
xcrun simctl boot "$UDID"
xcrun simctl bootstatus "$UDID" -b

# The home screen. Any later screenshot that still looks like this one is a
# screenshot of nothing.
xcrun simctl io "$UDID" screenshot "$OUT/00-home.png" || true

xcrun simctl install "$UDID" "$APP" 2>&1 | tee /tmp/sim-install.log
# --console-pty surfaces stdout/stderr, which is where a wx assert goes.
xcrun simctl launch --console-pty "$UDID" "$BUNDLE_ID" > "$OUT/sim-launch.log" 2>&1 &
LPID=$!

EXE=$(basename "$APP" .app)

# How NOT to ask whether a simulator app is running: `simctl spawn <udid>
# launchctl list`. It does not list app processes under their bundle id, so it
# answers "no" for an app that is plainly up - wx probe run 2 reported "never
# alive" for a process the system log shows as PID 4992 with a full UIKit scene
# stack, and no screenshot was taken of a perfectly healthy app.
#
# A simulator app is an ordinary process on the host, and `simctl launch` prints
# its host pid as "<bundle id>: <pid>". That pid is the exact answer, and
# `kill -0` on it costs nothing - which also keeps the checkpoints honest,
# because a per-poll `simctl spawn` took the better part of a second each time.
APP_PID=""
for i in $(seq 1 20); do
  APP_PID=$(sed -n "s/^${BUNDLE_ID}: \([0-9][0-9]*\).*/\1/p" "$OUT/sim-launch.log" 2>/dev/null | head -1)
  [ -n "$APP_PID" ] && break
  sleep 1
done
echo "launch reported pid: ${APP_PID:-none}"

alive() {
  # OR of every method that can work, so this can only ever be more sighted
  # than the last version. pgrep first: it needs no assumption about whose pid
  # namespace simctl printed.
  pgrep -f "\.app/${EXE}" >/dev/null 2>&1 && return 0
  pgrep -x "$EXE" >/dev/null 2>&1 && return 0
  [ -n "$APP_PID" ] && kill -0 "$APP_PID" 2>/dev/null && return 0
  xcrun simctl spawn "$UDID" launchctl list 2>/dev/null | grep -q "$BUNDLE_ID" && return 0
  return 1
}

# Which of them actually sees a just-launched app, recorded once. Two probe runs
# have now disagreed with the system log about whether the app was running, and
# guessing at the answer costs a run each time; this settles it with evidence in
# the log rather than another hypothesis.
{
  echo "=== immediately after launch (pid line: ${APP_PID:-none}) ==="
  echo "--- host: ps -p \$APP_PID"
  [ -n "$APP_PID" ] && ps -p "$APP_PID" -o pid,stat,etime,comm 2>&1 || echo "(no pid)"
  echo "--- host: pgrep -fl \$EXE"
  pgrep -fl "$EXE" 2>&1 || echo "(no match)"
  echo "--- host: ps -Ao pid,comm | grep EXE"
  ps -Ao pid,comm 2>/dev/null | grep -i "$EXE" || echo "(no match)"
  echo "--- sim: launchctl list | grep bundle id"
  xcrun simctl spawn "$UDID" launchctl list 2>&1 | grep -i "$BUNDLE_ID" || echo "(no match)"
  echo "--- sim: launchctl list line count"
  xcrun simctl spawn "$UDID" launchctl list 2>/dev/null | wc -l
  echo "--- sim: launchctl list, first 15 lines"
  xcrun simctl spawn "$UDID" launchctl list 2>/dev/null | head -15
} > "$OUT/alive-methods.txt" 2>&1
cat "$OUT/alive-methods.txt"

FIRST_ALIVE=""
for i in $(seq 1 "$STARTUP_TIMEOUT"); do
  if alive; then FIRST_ALIVE="$i"; break; fi
  sleep 1
done
echo "first seen alive after: ${FIRST_ALIVE:-never} s"

SHOTS=0
LAST_ALIVE_AT=""
LAST_CHECKPOINT=""
for T in $CHECKPOINTS; do LAST_CHECKPOINT="$T"; done
if [ -n "$FIRST_ALIVE" ]; then
  PREV=0
  for T in $CHECKPOINTS; do
    sleep $((T - PREV)); PREV=$T
    if ! alive; then
      echo "app exited before t=${T}s"
      break
    fi
    LAST_ALIVE_AT="$T"
    xcrun simctl io "$UDID" screenshot "$OUT/ui-t$(printf '%02d' "$T")s.png" && SHOTS=$((SHOTS + 1))
  done
fi

# The last screenshot taken while the process was alive answers "does the UI
# come up", so give it a stable name.
LATEST=$(ls -1 "$OUT"/ui-t*.png 2>/dev/null | tail -1 || true)
[ -n "$LATEST" ] && cp "$LATEST" "$OUT/orca-on-ipad.png"

ALIVE_ENTRIES=0
alive && ALIVE_ENTRIES=1
kill "$LPID" 2>/dev/null || true
wait "$LPID" 2>/dev/null

# Screenshots are for a human, but the job has to decide on its own whether
# anything was drawn. Decoding a 2064x2752 PNG scanline by scanline in Python
# takes minutes, so sips shrinks each one to 240 px first - plenty for colour
# statistics, and instant to decode.
rm -rf /tmp/small && mkdir -p /tmp/small
for f in "$OUT"/*.png; do
  [ -e "$f" ] || continue
  sips -Z 240 --out "/tmp/small/$(basename "$f")" "$f" >/dev/null 2>&1 || true
done
python3 - /tmp/small/*.png > "$OUT/screenshot-stats.txt" 2>&1 <<'PNGSTAT'
import struct, sys, zlib
from collections import Counter

def stats(path):
    raw = open(path, 'rb').read()
    pos, idat, w, h, bpp = 8, b'', 0, 0, 4
    while pos < len(raw):
        ln = struct.unpack('>I', raw[pos:pos + 4])[0]
        typ = raw[pos + 4:pos + 8]
        body = raw[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            bpp = {0: 1, 2: 3, 4: 2, 6: 4}[color]
        elif typ == b'IDAT':
            idat += body
        pos += ln + 12
    data = zlib.decompress(idat)
    stride = w * bpp
    prev, out, i = bytearray(stride), [], 0
    for _ in range(h):
        f = data[i]; i += 1
        line = bytearray(data[i:i + stride]); i += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:   line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out.append(bytes(line)); prev = line
    px = Counter()
    for y in range(0, h, 2):
        row = out[y]
        for x in range(0, w, 2):
            px[row[x * bpp:x * bpp + 3]] += 1
    total = sum(px.values())
    top, topn = px.most_common(1)[0]
    return w, h, len(px), topn / total, top

for p in sys.argv[1:]:
    try:
        w, h, ncol, share, top = stats(p)
        print('%-24s %dx%d colours=%-5d top=#%s %.0f%%'
              % (p.split('/')[-1], w, h, ncol, top.hex(), share * 100))
    except Exception as e:
        print('%-24s unreadable: %s' % (p.split('/')[-1], e))
PNGSTAT
cat "$OUT/screenshot-stats.txt"

# The app's own log, out of its data container. The system log only ever shows
# UIKit's half of the story; this is the half that says how far startup got.
DATA=$(xcrun simctl get_app_container "$UDID" "$BUNDLE_ID" data 2>/dev/null)
if [ -n "${DATA:-}" ]; then
  echo "app container: $DATA"
  find "$DATA" -name "*.log" -exec sh -c 'echo "--- $1 ---"; tail -c 40000 "$1"' _ {} \; \
    > "$OUT/orca-app-log.txt" 2>&1 || true
  tail -80 "$OUT/orca-app-log.txt" || true
fi
xcrun simctl spawn "$UDID" log show --last 5m \
  --predicate "process == \"$(basename "$APP" .app)\"" > "$OUT/sim-oslog.txt" 2>&1 || true
tail -80 "$OUT/sim-launch.log" || true

{
  echo "simulator runtime: ${SIM_RUNTIME}"
  echo "first seen alive after: ${FIRST_ALIVE:-never} s"
  echo "still alive at t=${LAST_ALIVE_AT:-none} s (last checkpoint ${LAST_CHECKPOINT}s)"
  echo "screenshots taken while alive: $SHOTS"
  echo "still running at the end: $ALIVE_ENTRIES (pid ${APP_PID:-none})"
  echo "crash reports: $(find "$HOME/Library/Logs/DiagnosticReports" -name "${EXE}*" 2>/dev/null | wc -l | tr -d ' ')"
} | tee "$OUT/launch-verdict.txt"
# NB the pattern is $EXE, not a hardcoded "OrcaSlicer*". It was the latter until
# now, so every wx-probe and smoke-app run printed "crash reports: 0" whatever
# happened -- the question was never actually asked of those two apps, and "it
# exits without crashing" was read off a search that could only ever find
# nothing. The reports themselves are copied out too, so a crash is diagnosable
# from the log commit instead of costing another run.
find "$HOME/Library/Logs/DiagnosticReports" -name "${EXE}*" -exec cp {} "$OUT/" \; 2>/dev/null || true
for f in "$OUT"/${EXE}*.ips "$OUT"/${EXE}*.crash; do
  [ -e "$f" ] || continue
  echo "=== crash report: $(basename "$f") ==="
  head -c 8000 "$f"
done > "$OUT/crash-reports.txt" 2>&1 || true
cp /tmp/sim-install.log "$OUT/" 2>/dev/null || true

if [ -z "$FIRST_ALIVE" ]; then
  echo "::error::$BUNDLE_ID never appeared in the simulator's process list"
  exit 1
fi
if [ "$LAST_ALIVE_AT" != "$LAST_CHECKPOINT" ]; then
  echo "::error::$BUNDLE_ID started but exited during startup (last alive at t=${LAST_ALIVE_AT:-0}s)"
  exit 1
fi
echo "$BUNDLE_ID stayed up for the whole ${LAST_CHECKPOINT}s window; see $OUT/orca-on-ipad.png"
