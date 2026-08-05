# Installing the unsigned IPA on your iPad

The IPAs this repo publishes are **unsigned on purpose**. Every sideloading tool
re-signs the app with your own Apple ID as part of installing it, so shipping a
signature would only get in the way.

Builds are attached to GitHub Releases:
<https://github.com/Toemeler/Orca-iOS-ipa/releases>

The `.ipa` is a direct download, so the whole thing can be done from the iPad —
no computer needed once your sideloader is set up.

## Which tool?

| Tool | Computer needed? | App lifetime | Works on |
|---|---|---|---|
| **SideStore** | Once, to pair | 7 days, refreshes on-device | iOS 14+ |
| **AltStore Classic** | Yes, on the same Wi-Fi, to install *and* refresh | 7 days | iOS 14+ |
| **Sideloadly** | Yes, cable or Wi-Fi | 7 days | iOS 7+ |
| **TrollStore** | Once | permanent, no refresh | only iOS 14.0–17.0 |

A **free** Apple ID gives you 3 sideloaded apps at a time and a 7-day signature
that has to be refreshed. A **paid** Apple Developer account ($99/yr) raises
that to a year and 10 apps. Nothing here needs a paid account.

**TrollStore is the nicest option if your iPad qualifies** — it installs
permanently with no refreshing and no Apple ID at all. It relies on a bug Apple
fixed after iOS 17.0, so it only works up to that version. Check your iPad's
version under Settings → General → About → Software Version before going down
this route.

Note the current builds set a minimum of **iOS 17.0**, which leaves only iOS
17.0 exactly in TrollStore's range. If you want a build that targets an older
iOS so TrollStore can take it, that is a one-line change to the workflow — ask.

## SideStore (recommended for iOS 17+)

1. Install SideStore following <https://sidestore.io/#get-started>. Setup needs
   a computer once, to generate a pairing file, plus the StosVPN profile that
   lets SideStore talk to itself on-device.
2. Download the `.ipa` from the Releases page in Safari on the iPad.
3. Open SideStore → **My Apps** → **+** → pick the downloaded `.ipa`.
4. Sign in with your Apple ID when asked. The app appears on your home screen.
5. Every 7 days, open SideStore and hit **Refresh All**. If the signature does
   lapse the app stops launching until you refresh — nothing is lost.

## AltStore Classic

1. Install AltServer on a Mac or PC and AltStore on the iPad:
   <https://altstore.io>.
2. Keep the iPad and computer on the same Wi-Fi.
3. Download the `.ipa` on the iPad, then AltStore → **My Apps** → **+** →
   select it.
4. Refresh from AltStore weekly, with AltServer running.

## Sideloadly

1. Get it from <https://sideloadly.io> (Windows or macOS).
2. Plug the iPad in, drop the `.ipa` onto the Sideloadly window, enter your
   Apple ID, press **Start**.

## TrollStore (iOS 14.0–17.0 only)

1. Install TrollStore for your exact iOS version:
   <https://ios.cfw.guide/installing-trollstore/>.
2. Download the `.ipa`, open it with TrollStore, tap **Install**.
3. Done permanently — no Apple ID, no 7-day expiry.

## First launch

iOS blocks apps signed with a personal Apple ID until you trust the certificate:

**Settings → General → VPN & Device Management → your Apple ID → Trust**

TrollStore installs need no such step.

## Troubleshooting

- **"Unable to install" / the icon installs but never launches** — almost always
  an expired 7-day signature. Refresh in SideStore/AltStore.
- **"App cannot be installed because its integrity could not be verified"** —
  the certificate is untrusted; do the Trust step above.
- **Hit the 3-app limit** — free Apple IDs allow 3 sideloaded apps. Remove one,
  or use a paid developer account.
- **The app installs but shows a black screen** — that is a real bug, not a
  sideloading problem. Please open an issue with your iPad model and iOS
  version.

## What is actually in these builds

Two different things get published, so check what you are downloading:

- **`OrcaSmoke-iPad.ipa`** — the wxWidgets foundation: a window, a button, and
  a live OpenGL ES canvas. It proves the toolkit, the renderer, and this whole
  build-and-sideload pipeline work natively on the iPad. It is not a slicer.
- **OrcaSlicer itself** — still being linked; see `PLAN.md` for the stage list
  and `HANDOFF.md` for exactly where that work stands.

The app requests no special entitlements. File sharing is enabled, so anything
it writes shows up in the Files app.
