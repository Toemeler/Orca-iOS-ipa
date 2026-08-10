# Orca-iOS-ipa

Native iPadOS port of [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) — the full
desktop application, compiled for arm64 iOS, driven by hardware keyboard and
mouse/trackpad pointer. No virtualization, no streaming, no touch redesign.

Built entirely on GitHub Actions macOS runners in verifiable stages (same model as
[blender-iOS-ipa](https://github.com/Toemeler/blender-iOS-ipa) and
[Rayforge-iOS-ipa](https://github.com/Toemeler/Rayforge-iOS-ipa)); the end product is an
unsigned `.ipa` for sideloading.

**Read [`PLAN.md`](PLAN.md) first** — it explains why this port is tractable
(Orca already has an OpenGL ES render path; wxWidgets ships a working iOS base port;
most Orca chrome is custom-drawn and portable) and lays out the stage-by-stage build.

## Stages

| Stage | Workflow | Proof artifact | Status |
|---|---|---|---|
| 1. Slicing core on iOS | `ios-step1-core-cli.yml` | G-code sliced inside an iPad simulator | ✅ done |
| 2. wxWidgets iPhone port | `ios-step2-wxwidgets.yml` | Screenshot of wx GLES canvas on iPad | ✅ done |
| 3. Full GUI link-up | `ios-step3-gui.yml` | Orca launch screenshot | ✅ done |
| 4. Device IPA | `ios-step4-device-ipa.yml` | unsigned `.ipa` release | ✅ done |
| 5. Feature parity | | webview/camera/export on device | ▶️ in progress |

Stage 5 as it stands on a real iPad: slicing, sending a job over the LAN (it
prints), the sidebar and its presets, project save and autosave, the Device page,
HMS printer error codes and the printer's camera stream all work. The known-open
items are listed at the end of [`HANDOFF.md`](HANDOFF.md) — read that file's last
section before picking the work up.

## Printer connection (Bambu LAN mode)

Orca reaches Bambu printers through a closed-source network plugin it downloads
per platform. There is no iOS build of it, and iOS will not load a
runtime-downloaded dylib in any case — so the port would slice and render
perfectly and never reach the printer.

`orca-overlay/src/slic3r/Utils/BambuLan*` replaces it with the documented
protocols: **MQTT 3.1.1 over TLS** on port 8883 for status and control, **FTPS**
on 990 to send the sliced 3mf, and SSDP discovery. It registers as the same
`"bbl"` printer agent the plugin used, so nothing above it changes.

`tools/bambu-lan/` holds a mock printer and a self test that links the shipping
sources — run `tools/bambu-lan/run-selftest.sh` on any host with OpenSSL and
libcurl headers.

**[`lan-test-app/`](lan-test-app/)** is a small iPad app that links the same
backend and nothing else, so the protocol can be tried against a real printer in
half a minute rather than a 40-minute Orca build: connect, jog, extrude, set
temperatures, fans, light, job control, raw gcode/JSON, and upload-and-print a
3mf. Built by `ios-lan-test-ipa.yml` and published to
[Releases](../../releases) as `BambuLAN.ipa`.

## Sideloadable builds

`ios-step4-device-ipa.yml` builds the full application against the **iphoneos**
SDK and publishes an unsigned, sideloadable `.ipa` to [Releases](../../releases)
— downloadable straight from the iPad. See
[`docs/SIDELOADING.md`](docs/SIDELOADING.md).

Every green run also publishes a SideStore/AltStore source and a flat manifest,
so a build can be installed in two taps from a push notification:

```
https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/source.json
https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/latest.json
```

[`docs/AUTOINSTALL.md`](docs/AUTOINSTALL.md) covers the Shortcut, the
notification setup and why the second tap cannot be removed.

The smaller `ios-device-ipa.yml` track is still there and still useful. It runs
against **stock, unpatched wxWidgets** on purpose: the app it ships only
needs widgets the iPhone port already provides, so this track stays green no
matter what state the step-3 patch stack is in. That keeps a working install
path on the iPad available at all times while the full GUI is still being linked.

Each stage's build breakages are fixed via ordered patches in `patches/stepN/` — never by
forking upstream. `analysis/` contains the generated wx symbol usage data and the gap
analysis that scopes the GUI work.

## Running a stage

Actions tab → select the step workflow → *Run workflow*. Inputs default to the pinned
upstream refs in `PLAN.md`. Expect the first runs to fail partway — that is the point:
each failure is converted into a patch and the stage is re-run until its artifact appears.

## License

AGPL-3.0, matching upstream OrcaSlicer. wxWidgets portions under the wxWindows licence.
