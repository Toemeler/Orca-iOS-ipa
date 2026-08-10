# AUTOINSTALL — from a green build to the iPad

Adapted from [Toemeler/ipadprocad](https://github.com/Toemeler/ipadprocad)'s
`AUTOINSTALL.md`, and mechanically the same: same two CI scripts, same two extra
release assets, same Shortcut, same reasoning about why it costs two taps.

```
CI (build green) --> Release: IPA + source.json + latest.json
                                      |
  [tap 1] run the Shortcut (Control Centre / Home Screen / widget / notification)
                                      v
  Shortcut: look up the newest build, VPN on, sidestore://install
                                      |
                      [tap 2] "Install" in SideStore
                                      v
                  download, sign, install
```

The Shortcut needs no service, no secret and no automation: every time it runs
it fetches `latest.json` and installs whatever the newest release is. The push
notification is optional on top — it is only the signal that something new is
ready, so you are not tapping into an empty release page.

## Why two taps and not none

This is the reason from ipadprocad, read out of SideStore's source rather than
its documentation, and it has not changed:

* `sidestore://install?url=…` lands in `SideStore/DeepLinks/URLHandler.swift`
  and reaches `MyAppsViewController.importApp`, which **always** calls
  `InstallAppDialog.present` — a `UIAlertController` with Install / Cancel.
  Opening a `.ipa` file goes through the same dialog.
* SideStore's only App Intents are `RefreshAllAppsIntent` and its widget
  variant. There is **no** install intent, so a Shortcut cannot make SideStore
  install in the background — only re-sign what is already installed.
* There is no auto-update from a source. `AppManager.backgroundRefresh` only
  re-signs installed apps.

With an unmodified SideStore that Install tap cannot be removed.

## What the CI produces

Every green run of `ios-step4-device-ipa` publishes the release
`orcaslicer-ipa-run<N>`:

| Asset | Contents |
| --- | --- |
| `OrcaSlicer-iPad.ipa` | the unsigned IPA, ~146 MB |
| `OrcaSlicer-iPad-minimal.ipa` | executable and Info.plist only, for install diagnosis |
| `source.json` | SideStore/AltStore source, last 10 builds, newest first |
| `latest.json` | flat manifest for the Shortcut |

Fixed entry points — GitHub always redirects `latest` to the newest release:

```
https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/source.json
https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/latest.json
https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/OrcaSlicer-iPad.ipa
```

The version in the source is the run number: run 104 appears as `1.0.104`, so
SideStore's list says which build is which. The last 15 releases are kept and
older ones pruned; that window is the way back when a build turns out broken on
the device.

## Setting it up on the iPad (once)

### 1. Add the source to SideStore

SideStore → Sources → **+** → the `source.json` URL above. Not required for the
two-tap path, but it is the way back: it lists the last ten builds with date and
commit subject, and any of them can be installed directly.

### 2. The "Install OrcaSlicer" Shortcut

The name must be **exactly** that if you also set up notifications — the CI puts
it into the notification's URL. Without notifications the name is yours to
choose.

The Shortcut takes no input; it looks up the newest build itself:

1. **Get contents of** `https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/latest.json`
2. **Get dictionary value** `ipaURL` from the result
3. **Set VPN** → On (SideStore needs its tunnel up before signing)
4. **Wait** 2 seconds
5. **Open URL** → `sidestore://install?url=` followed by the value from step 2

Then tap Install in SideStore.

### 3. Notifications (optional)

Set either service's secrets in the repository — the workflow step is a no-op
without them and never fails the build:

| Secret | Service |
| --- | --- |
| `NTFY_TOPIC` (and optionally `NTFY_URL`) | [ntfy.sh](https://ntfy.sh) |
| `PUSHOVER_TOKEN` + `PUSHOVER_USER` | Pushover |

Subscribe to the same topic in the ntfy app. Each green build sends a
notification whose action opens the Shortcut.

**The notification deliberately does not carry the IPA URL.** An ntfy topic is
public to anyone who knows its name, and ntfy's iOS client hands the click URL
straight to `UIApplication.open` without inspecting it — so a notification
carrying a download URL would be a stranger's lever to feed an arbitrary IPA
into your install Shortcut. The Shortcut resolves `latest.json` itself, so a
spoofed notification can at worst offer you the genuine newest build.
