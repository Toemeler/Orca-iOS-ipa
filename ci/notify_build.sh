#!/usr/bin/env bash
#
# Ping the iPad when a build is installable. Best effort — a failed push must
# never turn a green build red, so every path here exits 0.
#
# Usage: ci/notify_build.sh <ipa-url> <build-number> <subject>
#
# Picks whichever service is configured; both may be set:
#   PUSHOVER_TOKEN + PUSHOVER_USER   -> api.pushover.net
#   NTFY_TOPIC (+ optional NTFY_URL) -> ntfy.sh
#
# The notification's URL is not the IPA and not the sidestore:// link — it is
# the SHORTCUT:
#
#   shortcuts://run-shortcut?name=<SHORTCUT_NAME>
#
# so that one tap can flip the VPN on first and only then hand the URL to
# SideStore. Tapping a bare sidestore:// link skips that and lands in SideStore
# with the tunnel down, which fails at signing. See AUTOINSTALL.md.
#
# NO `input=text&text=<ipa url>`, deliberately. An ntfy topic is public to
# anyone who knows its name, and ntfy's iOS client hands the click URL straight
# to UIApplication.open (AppDelegate.swift) without inspecting it — so a
# notification that carried the download URL would be a stranger's lever to
# feed an arbitrary IPA into the install shortcut. The shortcut resolves
# latest.json itself instead; then a spoofed notification can at worst make the
# iPad offer the genuine newest build.

set -uo pipefail

IPA_URL="${1:-}"
BUILD="${2:-?}"
SUBJECT="${3:-}"

# The URL is not sent anywhere (see above) — it is the proof that a release
# actually got published. Nothing to announce without one.
[ -n "$IPA_URL" ] || { echo "notify: no IPA URL, skipping"; exit 0; }

SHORTCUT_NAME="${SHORTCUT_NAME:-Install OrcaSlicer}"

# RFC 3986 encoding for the query values (jq is on every runner; -r @uri is
# exactly the escaping we need, including the spaces in the shortcut name).
enc() { printf '%s' "$1" | jq -sRr @uri; }

RUN_URL="shortcuts://run-shortcut?name=$(enc "$SHORTCUT_NAME")"
TITLE="Build ${BUILD} ready"

sent=0

if [ -n "${PUSHOVER_TOKEN:-}" ] && [ -n "${PUSHOVER_USER:-}" ]; then
  if curl -fsS --max-time 20 https://api.pushover.net/1/messages.json \
      --form-string "token=${PUSHOVER_TOKEN}" \
      --form-string "user=${PUSHOVER_USER}" \
      --form-string "title=${TITLE}" \
      --form-string "message=${SUBJECT}" \
      --form-string "url=${RUN_URL}" \
      --form-string "url_title=Install build ${BUILD}" \
      --form-string "priority=0" \
      -o /dev/null
  then echo "notify: pushover sent"; sent=1
  else echo "notify: pushover failed (non-fatal)"
  fi
fi

if [ -n "${NTFY_TOPIC:-}" ]; then
  NTFY_URL="${NTFY_URL:-https://ntfy.sh}"
  if jq -n --arg topic "$NTFY_TOPIC" --arg title "$TITLE" \
        --arg msg "$SUBJECT" --arg click "$RUN_URL" --arg b "$BUILD" \
        '{topic: $topic, title: $title, message: $msg, click: $click,
          actions: [{action: "view", label: ("Install " + $b), url: $click}]}' \
     | curl -fsS --max-time 20 -H "Content-Type: application/json" -d @- "$NTFY_URL" -o /dev/null
  then echo "notify: ntfy sent"; sent=1
  else echo "notify: ntfy failed (non-fatal)"
  fi
fi

[ "$sent" = 1 ] || echo "notify: no push service configured (set PUSHOVER_TOKEN/PUSHOVER_USER or NTFY_TOPIC)"
exit 0
