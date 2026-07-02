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
| 1. Slicing core on iOS | `ios-step1-core-cli.yml` | G-code sliced inside an iPad simulator | scaffolded — run + iterate |
| 2. wxWidgets iPhone port | `ios-step2-wxwidgets.yml` | Screenshot of wx GLES canvas on iPad | scaffolded — run + iterate |
| 3. Full GUI link-up | (added after 1+2 are green) | Orca launch screenshot | planned |
| 4. Device IPA | | unsigned `.ipa` release | planned |
| 5. Feature parity | | webview/camera/export on device | planned |

Each stage's build breakages are fixed via ordered patches in `patches/stepN/` — never by
forking upstream. `analysis/` contains the generated wx symbol usage data and the gap
analysis that scopes the GUI work.

## Running a stage

Actions tab → select the step workflow → *Run workflow*. Inputs default to the pinned
upstream refs in `PLAN.md`. Expect the first runs to fail partway — that is the point:
each failure is converted into a patch and the stage is re-run until its artifact appears.

## License

AGPL-3.0, matching upstream OrcaSlicer. wxWidgets portions under the wxWindows licence.
