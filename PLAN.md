# OrcaSlicer → iPadOS: full native port plan

Goal: a real, natively compiled OrcaSlicer.app for iPadOS (arm64), feature-identical to the
desktop build, driven by hardware keyboard + mouse/trackpad pointer. No virtualization, no
streaming, no touch redesign. Distribution as an unsigned IPA built by GitHub Actions
(macos-15 runners with Xcode), sideloaded via AltStore/SideStore/TrollStore — same model as
`blender-iOS-ipa` and `Rayforge-iOS-ipa`.

Upstream pin: `SoftFever/OrcaSlicer` @ `395e070a0e675fd4723f93967cefede730c482d9`
wxWidgets pin: `SoftFever/Orca-deps-wxWidgets` @ `v3.3.2`

## Why this is tractable (findings from source analysis, 2026-07-02)

1. **The renderer already speaks OpenGL ES.** Orca inherits PrusaSlicer's
   `SLIC3R_OPENGL_ES` compile path (see `OpenGLManager`, `GLShadersManager`, `GLModel`,
   `ImGuiWrapper`). iOS ships EAGL/GLES3. No ANGLE, no Metal rewrite needed for v1.
2. **wxWidgets ships a working iOS base port.** `src/osx/iphone/` implements the hard
   foundation: `window`, `nonownedwnd`, `evtloop`, `glcanvas` (EAGL), `textctrl`, `menu`,
   `dialog`, `notebook`, `toolbar`, buttons/checkbox/choice/slider/etc. (24 files — see
   `analysis/wx-iphone-native-files.txt`). Everything else in wx falls back to **generic**
   (self-drawn) implementations on top of that base: wxAUI, wxGrid, wxDataViewCtrl,
   wxSplitterWindow, wxScrolled, sizers, wxGraphicsContext (CoreGraphics, shared with macOS).
3. **Orca draws most of its chrome itself.** `src/slic3r/GUI/Widgets/` (Button, ComboBox,
   DropDown, TabCtrl, SideTools, ProgressBar, …) are wxWindow subclasses painted with wxDC —
   they run wherever the base port runs. The 3D scene UI (gizmos, toolbars, overlays) is
   Dear ImGui inside the GL canvas — fully portable.
4. **iPadOS 13.4+ delivers real pointer + hardware keyboard events** through UIKit
   (`UIPointerInteraction`, indirect touches, `UIKey`); these map onto wx mouse/key events
   in the iPhone port's `window.mm`/`evtloop.mm`. Desktop interaction model preserved.

Orca uses 505 distinct `wx*` symbols (`analysis/wx-symbols-used-by-orca.txt`). The gap
analysis (`analysis/GAP.md`) sorts them into native-available / generic-available /
must-implement / must-stub. The must-implement list is small and concentrated (webview,
clipboard, a few dialogs) — that is the actual porting work.

## Dependency stack (from `deps/`) — iOS status

| Dep | iOS path | Notes |
|---|---|---|
| Boost | ✅ official | `b2` with `target-os=iphone`; Orca uses filesystem/log/nowide/… |
| TBB | ✅ | oneTBB builds for iOS (static) |
| Eigen, Cereal, NanoSVG, EXPAT, ZLIB, PNG, JPEG, FREETYPE, NLopt, Qhull, libnoise | ✅ | plain CMake cross-compiles |
| OpenSSL, CURL | ✅ | standard iOS recipes (`ios64-xcrun` config / curl `--with-secure-transport` optional) |
| CGAL + GMP + MPFR | ✅ | GMP/MPFR have known iOS builds (assembly off / generic C) |
| OpenCV | ✅ official iOS support | build core+imgproc only (what Orca links) |
| OCCT (STEP import) | ✅ officially supports iOS | static, TKernel..TKXDESTEP subset |
| OpenVDB + Blosc + OpenEXR(Imath) | ✅ | static cross-compile |
| Draco | ✅ | CMake |
| GLEW / GLFW | ❌ **dropped** | not used in the `SLIC3R_OPENGL_ES` path (EAGL + GLES3 headers instead) |
| wxWidgets | ⚠️ the work | build Orca's wx fork with `CMAKE_SYSTEM_NAME=iOS` → iPhone port + generic widgets |
| WebView2 | n/a | Windows-only dep, skipped on Apple builds already |

## Platform-code deltas (small, surgical — kept in `patches/`)

- **Process spawning** (`Utils/Process.cpp`, Downloader, single-instance): iOS has no
  fork/exec. New-instance & "open in explorer" paths → no-op/`UIApplication openURL`.
  `InstanceCheckMac.mm` → trivial (one instance by definition).
- **wxWebView** (35 GUI files reference it — device page, login, guides): macOS backend
  already wraps WKWebView; WKWebView is the same class on iOS. Port
  `src/osx/webview_webkit.mm` to UIKit ⇒ highest-value native task.
- **RemovableDriveManager** (SD-card export): map to `UIDocumentPickerViewController` /
  app sandbox `Documents/` (files app). Feature preserved, backend swapped.
- **MediaPlayCtrl/wxMediaCtrl2** (printer camera): AVPlayer-backed shim (same approach as
  the mac build's AVFoundation path); BambuSource plugin is a dylib — dlopen of embedded
  frameworks works on iOS when bundled.
- **Paths/config**: `data_dir` → app sandbox `Application Support`; already abstracted in
  `libslic3r/utils`. Networking (Bonjour discovery, MQTT, FTP) is plain sockets/CURL — fine.

## Staged build (each stage = one workflow, one artifact, verifiable)

- **Step 1 — `ios-step1-core-cli.yml`** *(in this repo, runnable now)*
  Cross-compile the non-GUI dependency chain + `libslic3r` with `-DSLIC3R_GUI=0`,
  **for the iOS simulator (arm64)**, produce `orca-cli`, then `xcrun simctl spawn` it in a
  booted iPad simulator to **slice a real STL to G-code on iOS** and upload the G-code as
  the artifact. This proves the entire slicing engine on iOS before any GUI work.
- **Step 2 — `ios-step2-wxwidgets.yml`** *(in this repo)*
  Build the Orca wx fork for iOS device+simulator (iPhone port, `wxUSE_GUI=1`, static),
  compile-run a minimal wx "hello + wxGLCanvas GLES triangle" app in the simulator,
  screenshot via `simctl io screenshot` as the artifact.
- **Step 3 — GUI link-up**: build full Orca (`SLIC3R_GUI=1`, `SLIC3R_OPENGL_ES=1`) against
  step-1+2 sysroots; burn down link errors with `patches/`; stub list tracked in GAP.md.
  Artifact: app binary + launch screenshot in simulator.
- **Step 4 — device IPA**: same for `iphoneos` SDK, assemble
  `Payload/OrcaSlicer.app` (Info.plist: `UIRequiresFullScreen`, file-sharing enabled,
  `UISupportedInterfaceOrientations` landscape, min iPadOS 17), zip → unsigned `.ipa`,
  attach to a GitHub Release. Sideload per `docs/SIDELOADING.md`.
- **Step 5 — feature parity pass**: WKWebView-backed wxWebView, AVPlayer camera shim,
  document-picker export, keyboard shortcut table verification, printer network tests.

## License

OrcaSlicer is AGPL-3.0; all patches and this repo are AGPL-3.0. wxWidgets under the
wxWindows licence. Full corresponding source stays public here — sideload distribution
keeps us clear of App Store/AGPL friction.
