# HANDOFF: OrcaSlicer → iPadOS native port (Orca-iOS-ipa)

Give this document to a new chat to continue the work. Everything needed is here.

## Goal (user's requirement)

Build a **real, natively compiled OrcaSlicer for iPadOS** — a 1:1 copy of the desktop
(macOS) app. Nothing missing, NOT touch-optimized: keyboard + mouse/trackpad driven,
same as desktop. No virtualization, no streaming. Deliverable: unsigned `.ipa` built by
GitHub Actions for sideloading (AltStore/SideStore/TrollStore). Follow the staged-CI
pattern of the user's other repos: `Toemeler/blender-iOS-ipa` and `Toemeler/Rayforge-iOS-ipa`
(each stage = one workflow producing a verifiable artifact; failures become ordered patches).

## Access

- Repo: **github.com/Toemeler/Orca-iOS-ipa** (user: Toemeler, id 87390876)
- Fine-grained PAT (user-provided for this task; push + API + workflow dispatch):
  `<PROVIDED-BY-USER-IN-CHAT (fine-grained PAT for this repo)>`
- Auth pattern used: `https://Toemeler:$TOKEN@github.com/Toemeler/Orca-iOS-ipa.git`,
  API `Authorization: Bearer $TOKEN`.
- IMPORTANT constraint of the assistant environment: GitHub **Actions log downloads are
  blocked** (redirect host not allowlisted). Workaround already built: every workflow has a
  final `if: failure()` step that commits error-log excerpts to `ci-logs/<stage>-run-N/`
  in the repo; read them via the contents API with `Accept: application/vnd.github.raw`.
- Dispatch runs: `POST /repos/Toemeler/Orca-iOS-ipa/actions/workflows/<file>.yml/dispatches`
  body `{"ref":"main"}` (expect 204; a just-pushed workflow may 404 for ~20s).
- The user occasionally edits the repo between sessions — `git pull --rebase` before pushing
  and watch for duplicate/conflicting files (this already happened once: a parallel
  `smoke-cli` harness + duplicate 0007 patch broke patch application; was consolidated).

## Upstream pins

- OrcaSlicer: `SoftFever/OrcaSlicer` @ `395e070a0e675fd4723f93967cefede730c482d9`
- wxWidgets: `SoftFever/Orca-deps-wxWidgets` @ `v3.3.2`
- Runner: `macos-15`, Xcode 16.4, iPhoneSimulator 18.5 SDK, arm64, `IOS_MIN=17.0`

## Status: 2 of 5 stages COMPLETE

| Stage | Workflow | Status |
|---|---|---|
| 1. Slicing core on iOS | `ios-step1-core-cli.yml` | ✅ DONE (run 19): libslic3r sliced a cube **inside an iPad simulator**, artifact `orca-ios-step1-core-cli` (20.2 MB: iOS binary + G-code) |
| 2. wxWidgets iPhone port | `ios-step2-wxwidgets.yml` | ✅ DONE: wx static libs for iOS + `WxSmoke.app` screenshot (frame, button, green GL canvas) on simulated iPad |
| 3. Full GUI link-up | `ios-step3-gui.yml` | ▶️ IN PROGRESS — run 1 dispatched (milestone 1: `ninja libslic3r_gui`); proactive macOS-API sweep of `src/slic3r` started (findings below) |
| 4. Device IPA | not written yet | pending |
| 5. Feature parity (webview/camera/export) | not written yet | pending |

## Key architectural findings (why this port works)

1. **Orca has an OpenGL ES render path**: `SLIC3R_OPENGL_ES=1` compiles GLCanvas3D,
   GLShadersManager, GLModel, ImGuiWrapper against GLES — matches iOS EAGL. No ANGLE/Metal.
2. **wx iPhone port is real**: 26 native units (window, evtloop, glcanvas via **GLKit
   GLKView**, textctrl, menus, dialogs…). Generic (self-drawn) widgets + Orca's own
   `GUI/Widgets/` custom controls run on top of that base.
3. **Orca's `main()` (OrcaSlicer.cpp) is GUI-entangled** — includes Plater/GLCanvas/GLFW
   unconditionally (CLI thumbnails). `SLIC3R_GUI=0` cannot build upstream main; step 1 used
   a custom harness instead (`ios/orca-core-cli.cpp` + `ios/nanosvg_impl.cpp`, target
   `orca-core-cli` added by patch 0007). Real main returns in step 3.
4. `NANOSVG_IMPLEMENTATION` normally lives in GUI's BitmapCache.cpp (hence the harness impl TU).
5. Analysis data in `analysis/`: 505 wx symbols used by Orca; GAP.md buckets them; the
   must-implement list ≈ wxWebView (WKWebView backend), clipboard, file/colour dialogs,
   wxMediaCtrl→AVPlayer, standard paths, single-instance no-op.

## Repo layout

```
PLAN.md                     full staged plan (read it)
analysis/GAP.md             wx gap analysis + buckets
patches/step1/0001..0008    all verified applying sequentially to pinned ref
patches/step2/              (empty so far — wx needed only build flags)
patches/step3/              (opened, empty — GUI source patches go here)
ios/orca-core-cli.cpp       step-1 slicing harness (+ nanosvg_impl.cpp)
smoke-app/                  step-2 wx GLKit smoke app + build.sh
test-assets/calibration-cube.stl
ci-logs/                    failure logs committed by workflows
docs/SIDELOADING.md
```

## The 8 step-1 patches (what they fix — needed knowledge for step 3 too)

- 0001: deps superbuild `ORCA_DEPS_GUI` option gates GLEW/GLFW/OpenCSG/wx; (also pass
  `-DCMAKE_SYSTEM_PROCESSOR=arm64` — empty var crashes two `list(FIND)` calls)
- 0002: OpenSSL uses `ios64-xcrun`/`iossimulator-xcrun`, `no-tests`; Linux cross branch
  guarded `AND NOT APPLE` (empty TOOLCHAIN_PREFIX produced `-gcc`)
- 0003: OpenCV needs `-DIOS=1` (+AVFoundation/CAP_IOS off) or it compiles AppKit sources
- 0004: top-level `find_package(OpenGL/glfw3)` gated behind `SLIC3R_GUI`; iOS branch sets
  `-framework OpenGLES`; **forces `IS_CROSS_COMPILE=TRUE` when CMAKE_SYSTEM_NAME=iOS**
  (arm64→arm64 fooled Orca's detection; host-run encoding-check tool then aborted)
- 0005: utils.cpp — libproc/proc_pidpath is macOS-only; iOS uses `_NSGetExecutablePath`
- 0006: GCodeSender — IOKit/serial + IOSSIOSPEED gated `TARGET_OS_OSX`
- 0007: adds `orca-core-cli` target (guarded by file existence; workflow copies sources in)
- 0008: GMP/MPFR (the only autotools deps) get `-isysroot` + `-mios-simulator-version-min`
  + `--host=aarch64-apple-darwin --disable-assembly`; cross branch guarded `NOT APPLE`.
  MPFR also needed **texinfo** installed (doc build wants makeinfo).

Other build-system knowledge: Homebrew's libjpeg leaked into the link → deps build their
own JPEG (`-DCMAKE_DISABLE_FIND_PACKAGE_JPEG=ON` + `dep_JPEG` target) and Orca configure
pins `JPEG_LIBRARY`/`JPEG_INCLUDE_DIR` to the prefix. iOS cross mode re-roots find_package
into the SDK → always pass `CMAKE_FIND_ROOT_PATH=<prefixes>` + `_MODE_*=BOTH`.
Deps caching: `actions/cache` restore-keys `ios-deps-v1-`, save `if: always()`; on restore,
wipe any `dep_*-prefix` lacking an `*-install` stamp.

## wx build flags that work (step 2, green)

```
-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator
-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
-DIPHONE=ON                # fork's gate only matches 'iphoneos' sysroot, not simulator
-DwxBUILD_TOOLKIT=osx_iphone
-DwxBUILD_SHARED=OFF -DwxBUILD_PRECOMP=OFF -DwxBUILD_SAMPLES=OFF
-DwxUSE_OPENGL=ON -DwxUSE_WEBVIEW=OFF -DwxUSE_XRC=OFF   # wxrc host tool can't be an iOS exe
-DwxUSE_MEDIACTRL=OFF -DwxUSE_SECRETSTORE=OFF
```
Linking wx apps additionally needs `-framework GLKit` (glcanvas is GLKView-based) plus
UIKit/OpenGLES/QuartzCore/CoreGraphics/CoreText/CoreFoundation/Foundation/Security
/AudioToolbox/CFNetwork/MobileCoreServices and `-lz -liconv -lexpat -llzma`.
Simulator proof pattern: `simctl create` "iPad-Pro-13-inch-M4-16GB" + latest runtime,
`simctl install/launch`, `simctl io <udid> screenshot`; step 1 used `simctl spawn` to run
the CLI binary directly.

## Step 3 — where it stands and what to do next

Workflow `ios-step3-gui.yml` (already in repo): rebuilds deps from cache + wx fresh, then
configures Orca `SLIC3R_GUI=1 SLIC3R_OPENGL_ES=1 SLIC3R_STATIC=1` with both prefixes on
`CMAKE_PREFIX_PATH`/`CMAKE_FIND_ROOT_PATH`, milestone target `ninja libslic3r_gui`.
Run 1 was in progress at handoff; check `ci-logs/step3-run-1/` for its error batch
(expect configure issues first: Orca's `find_package(wxWidgets)` may need
`-DwxWidgets_CONFIG_EXECUTABLE=$WXPREFIX/bin/wx-config`; also GLEW/glad questions under
SLIC3R_OPENGL_ES — check how `src/slic3r/CMakeLists.txt` and `src/CMakeLists.txt` (glad
subdir) behave with ES enabled).

Proactive macOS-API sweep of `src/slic3r` (completed, patches NOT yet written) — the
files needing `TARGET_OS_OSX` guards or iOS stubs, destined for `patches/step3/`:
- `.mm`: DeepLinkHandlerMac, GUI_UtilsMac, InstanceCheckMac (→ no-op on iOS),
  Mouse3DHandlerMac (3Dconnexion → stub), RemovableDriveManagerMM (DiskArbitration →
  stub now, UIDocumentPicker later), wxMediaCtrl2.mm (AVFoundation; partial iOS reuse),
  Utils/MacDarkMode.mm (NSAppearance → UIUserInterfaceStyle or stub),
  Utils/RetinaHelperImpl.mm (NSScreen/backingScale → UIScreen.scale)
- `.cpp` with AppKit/mac APIs: GUI/GUI.cpp, GUI/GUI_App.cpp, GUI/SendSystemInfoDialog.cpp,
  Utils/Serial.cpp (IOKit serial enumeration → stub list on iOS)
- Also check `src/slic3r/CMakeLists.txt` APPLE framework list (AppKit/IOKit/DiskArbitration
  must become UIKit-era equivalents on iOS) and which .mm files it compiles under APPLE.
- wxWebView: wx was built `wxUSE_WEBVIEW=OFF`, but ~35 Orca GUI files include wx/webview.h.
  Interim plan: Orca-side compile-out or minimal stub; proper plan (step 5): enable
  wx webview with a WKWebView iPhone backend (macOS backend is already WKWebView — port it).

## The iterate loop (how all progress was made)

1. Dispatch workflow via API → wait → user says "failed"/"continue".
2. Read `ci-logs/<stage>-run-N/*-errors.log` (grep'd FAILED/error context).
3. Reproduce/patch against local sparse clones (`/home/claude/orca`, wx fork), regenerate
   `git diff` into `patches/<stage>/NNNN-*.patch`; ALWAYS verify the whole patch dir applies
   sequentially to a pristine clone before pushing.
4. Commit with a message documenting run number + root cause; `git pull --rebase`; push;
   dispatch; repeat. Steps 1+2 took ~25 runs total; expect step 3 to take more.

## Remaining stages after step 3

- Milestones within 3: libslic3r_gui compiles → OrcaSlicer app links (restore real main;
  needs GLFW decision for CLI-thumbnail path — likely gate that code off on iOS) →
  launches in simulator (screenshot artifact).
- Step 4: same build against `iphoneos` SDK; `Payload/OrcaSlicer.app` with Info.plist
  (UIDeviceFamily 2, file sharing on, UILaunchScreen); zip → unsigned .ipa → GitHub Release.
- Step 5: WKWebView-backed wxWebView (device page/login), AVPlayer camera shim,
  UIDocumentPicker export, clipboard, keyboard shortcut verification.

## Conduct notes

- All patches are original work, AGPL-3.0, kept in-repo (upstream sources never forked).
- Advise the user to rotate/revoke the PAT after sessions (it appears in chat history).

---
## UPDATE (session 2): Step 3 progress — libvgcode DONE, into main GUI bulk

Step 3 milestone-1 burn-down continued. libvgcode (Orca's toolpath renderer)
fully compiles now. Patches added:
- step3/0303: hidapi iOS no-op backend (ios/hid_ios_stub.c copied to deps_src/hidapi/ios/hid.c)
- step3/0304: libvgcode ENABLE_OPENGL_ES path — Vec4 added to libvgcode Types.hpp,
  set_positions/set_heights_widths_angles take Vec4, texture uploads RGB->RGBA,
  both ES call sites (init_impl + update_heights_widths) pass Vec4. (Upstream ES-path bug.)
- step3/0305: glad gles2.c — the two `eglGetProcAddress` statements guarded in place
  (`#if defined(__APPLE__)` null proc-ptr / skip null-check). iOS links GLES from the
  OpenGLES framework; no EGL/dlopen runtime loader. NOTE: function-wrapping diffs kept
  mis-placing #endif over 4 runs — the working approach guards the *statements*, verified
  with a preprocessor-branch trace (awk) that neither egl line is active under __APPLE__.

**LESSON (important for next agent):** regenerate every patch with `git diff` from a
PRISTINE upstream clone in a scratch dir, never from a working copy that already has your
edits — partial diffs bit us repeatedly. And after writing a #if/#else/#endif guard,
verify with a branch trace, not by eyeballing.

### wxWidgets iPhone-port gaps now surfacing (step 2 patches, applied by step2 AND step3)
Main GUI bulk (GUI_Utils.hpp etc.) needs wx classes the fork's iPhone port lacks:
- step2/0201 + wx-overlay/: fixed `wx/osx/evtloop.h` (hardcoded cocoa include; added
  `__WXOSX_IPHONE__` branch) and added a new `wx/osx/iphone/evtloop.h` (the iphone
  evtloop.mm implements `wxGUIEventLoop : wxCFEventLoop` but no header existed). The new
  header ships via `wx-overlay/` copied into wx `include/` after patches apply.
- **STILL TODO (the next real work):** the iPhone port has NO `wxFileDialog` and NO
  `wxStaticBox` (GUI_Utils.hpp uses both heavily; also `wxFileSelectorPromptStr`,
  `wxFileSelectorDefaultWildcardStr`, `wxFD_*`, `wxFileDialogNameStr`). Two options:
  (a) implement minimal iPhone versions in the wx fork (filedlg.mm →
  UIDocumentPicker; statbox → a plain wxStaticBox generic), shipped as step2 patches +
  overlay .mm/.h; or (b) Orca-side: gate the wxFileDialog-using code paths on iOS and
  provide a document-picker shim. (a) is cleaner and unblocks more. wxStaticBox generic
  may already exist in wx (`src/generic`)—check `wxUSE_STATBOX`/generic before writing one.
- Recurring theme: each such class unblocks many Orca files at once, so fix at the wx layer.

### Step 3 workflow milestone target is still `ninja libslic3r_gui` (milestone 1).
After it compiles: restore Orca's real main() (gate its GLFW/thumbnail CLI path off on
iOS), link the app, launch in simulator. Then step 4 (device IPA), step 5 (feature parity).

---
## UPDATE (session 2, cont.): Step 3 deep in wxWidgets iPhone-port control enablement

Milestone-1 (`ninja libslic3r_gui`) is blocked in the **wxWidgets build phase** now
(step 5/8), not Orca. Root discovery: the fork's iPhone port ships a **deliberately
minimal widget set** — `OSX_IPHONE_SRC` in `build/cmake/files.cmake` is a FIXED list of
24 .mm files (NOT flag-driven like desktop ports), and `include/wx/osx/iphone/chkconf.h`
hard-disables ~79 `wxUSE_*` controls. Orca needs many of them.

Patches added this session (all step2, applied by step2 AND step3 workflows):
- 0201 + wx-overlay/: evtloop.h iPhone branch + new iphone/evtloop.h (declares the
  5 methods evtloop.mm defines; MUST be declarations only — inline WakeUp collided).
  Overlay is copied into wx source include AND into WXPREFIX after `ninja install`.
- 0202: chkconf.h re-enables 24 flags (STATBOX, STATLINE, CHECKLISTBOX, FILEDLG,
  FILECTRL, FILEPICKERCTRL, DIRDLG, LISTCTRL, IMAGLIST, HEADERCTRL, VALIDATORS, SPINBTN,
  SPINCTRL, COLLPANE, RADIOBTN/BOX, TOGGLEBTN, PROGRESSDLG, TEXTDLG, NUMBERDLG, CHOICEDLG,
  FINDREPLDLG, ABOUTDLG, STATUSBAR) + routes filedlg & colordlg umbrellas to wx GENERIC
  backends on __WXOSX_IPHONE__ (osx native needs NSOpenPanel/NSColorPanel).
- 0203: adds the implementation sources to OSX_IPHONE_SRC — native-osx controls with no
  AppKit deps (statbox_osx, statline_osx, radiobut_osx, radiobox_osx, spinbutt_osx) + wx
  generic controls (listctrl, imaglist, headerctrlg, spinctlg, collpaneg,
  collheaderctrlg, filectrlg, filedlgg, dirdlgg, colrdlgg, progdlgg, textdlgg, numdlgg,
  choicdgg, fdrepdlg) + validators (valgen/validate/valtext) + fldlgcmn + filectrlcmn.

### CURRENT BLOCKER (step3 run-14, 57 wx errors) — UNSOLVED, precise diagnosis:
`wx/generic/filectrlg.h:158` uses `wxListEvent` (also wxListItem, wxLC_LIST) → "unknown
type name". BUT `wxUSE_LISTCTRL` is provably 1 after chkconf (verified: chkconf.h:156
`#define wxUSE_LISTCTRL 1` unconditional; global chkconf.h:1627 even force-enables it).
`wxListEvent` is declared in `wx/listbase.h:512` (ungated), included by `wx/listctrl.h`
(line-15 `#if wxUSE_LISTCTRL`), included by filectrlg.h line 16.
**Hypothesis:** include-guard poisoning — something includes `wx/listctrl.h` EARLIER in the
TU while `wxUSE_LISTCTRL` is momentarily 0 (before chkconf fixups apply), setting guard
`_WX_LISTCTRL_H_BASE_` so the later include is a no-op and the class decls are skipped.
Needs empirical bisection on the runner (expensive at ~30min/run). Approaches to try next:
  1. Add `-DwxUSE_LISTCTRL=1` etc. via CMake `-D` (wx honors some via cache) OR edit the
     base `include/wx/osx/setup.h` values too (they're already 1 there — so this may not
     be it), to make the flag 1 from the very first include.
  2. Find what includes listctrl.h early: check wx/osx/iphone/private.h, window.h,
     toolbar.mm precompiled path. `grep -rl listctrl include/wx/osx` came back empty, so
     the early include is likely transitive via a common header — instrument with
     `-H` (clang header trace) in a throwaway run to see include order.
  3. ALTERNATIVE STRATEGY (recommended if bisection stalls): stop enabling generic
     filedlg/filectrl/listctrl entirely. Instead keep them DISABLED and provide a small
     iOS-native `wxFileDialog` shim (subclass wxFileDialogBase) backed by
     UIDocumentPickerViewController, shipped as a wx overlay .mm+.h. This sidesteps the
     entire listctrl/validator/imaglist dependency tree (filectrlg pulls all of it).
     Orca only needs wxFileDialog's open/save result, not the generic file browser UI.
     This is likely LESS total work than making the full generic stack compile on iphone.

### Honest status
Step 3 is the hard stage (flagged from the start). Steps 1–2 remain green. libvgcode +
hidapi + the Orca GUI .cpp files that compiled before the wx-phase regression are fine.
The wx control-enablement is a real sub-project; the UIDocumentPicker shim (approach 3)
is the most promising path and is where a fresh session should probably start.

═══════════════════════════════════════════════════════════════════════
## SESSION UPDATE (2026-07-07, post run-37) — tier 7 + infra
═══════════════════════════════════════════════════════════════════════
Runs 24–36 walked step-3 failures 273→22 (the "273" was 3 shared-header
errors seen through a stale wx cache; the cache-key hash fix landed earlier).
This session (commits ea1d0f3..d2ac8d5) addressed all 22 known failures:

wx side (0202/0203 regenerated; WX_KEY auto-changes):
- enabled DIRDLG TEXTDLG CHOICEDLG ANIMATIONCTRL STATUSBAR MENUBAR
- wxDirDialog = new UIDocumentPicker folder-mode shim (wx-overlay
  iphone/dirdlg.h + .mm, mirrors the filedlg shim); generic dirdlgg.cpp
  guarded out (drags wxDirCtrl/wxTreeCtrl)
- webview: REAL wx/webview.h + src/common/webview.cpp{,archive,fs} compiled
  backendless (all wxUSE_WEBVIEW_* = 0; master chkconf's silent
  "requires a backend" re-disable waived for __WXOSX_IPHONE__). All 5
  overlay webview stubs DELETED — Orca's Widgets/WebView.cpp overrides
  ~20 wxWebView virtuals with `override`; only the real header satisfies
  that (audited: all 32 overrides exist in the real class). New()->NULL,
  Orca's backend-unavailable path handles it; step 5 = WKWebView backend.
- MENUBAR: the port ships a full UIMenuBuilder implementation
  (iphone/menu.mm OSXOnBuildMenu) — flag-on activates real iPad system
  menus. Needed because MainFrame constructs wxMenuBar unconditionally,
  AND because STATUSBAR=1 activated framecmn.cpp ShowMenuHelp which calls
  MENUBAR-gated FindItemInMenuBar — exactly run 37's single wx failure.
  Watch for more "flag A calls API of flag B" interactions.

Orca side: 0310 (ObjectList native-renderer calls iphone-guarded),
0311 (Serial: IOKit/IOSSIOSPEED macOS-only, iOS→plain termios),
0312 (SendSystemInfo: IOKit UUID macOS-only, iOS→machine-id branch→""),
0313 (CloudAgent: gethostuuid macOS-only, iOS→wxGetHostName),
0302 amended (wxMediaCtrl2.cpp is Win/Linux-only — iOS compiles NEITHER
variant, .h declaration suffices; .mm impl is step 5 AVPlayer).

infra: ccache wired into both step3 workflows (launchers on Orca configure
only; cache saved even on failure) — open issue #2 addressed; first warm
run populates, then iteration = recompile-changed-TUs only. Error-log
excerpt cap 70KB→400KB (run-36 was truncated mid-error; webview errors
were never visible). SUMMARY "error signatures" grep fixed (the '"'"'
escapes inside a double-quoted string matched nothing since day one).
WX_KEY now hashes ALL overlay files recursively incl. the file list.
tools/wx-sdk-stubs got TARGET_OS_MAC so the local probe reaches
platform.h's Darwin branch (before this, probes silently validated the
DESKTOP config). Faithful probe invocation:
  gcc -fsyntax-only -D__DARWIN__ -D__MACH__ -D__WXOSX__ -D__WXOSX_IPHONE__ \
      -D__WXMAC__ -I /tmp/wxtest -I include -I tools/wx-sdk-stubs probe.cpp
(probe caveat: wxUSE_MENUS resolves OFF locally — it's gated on
__IPHONE_OS_VERSION_MAX_ALLOWED>=130000 which only the real SDK defines.)

Known-remaining risks for run 38+: Plater/GUI_App/MainFrame may have
second-layer errors previously masked by the first error in each TU;
PhysicalPrinterDialog + UpdateDialogs + PresetUpdater + GUI_Preview +
DragDropPanel + GUI_AuxiliaryList + NetworkPluginDialog + GUI_ObjectTable*
failures from run 35 were never root-caused individually (some were fixed
by tiers 5–6, run 36 had 22 left). The 400KB log + fixed signatures will
finally show the full picture.
