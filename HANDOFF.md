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
| 3. Full GUI link-up | `ios-step3-gui.yml` | ▶️ IN PROGRESS — milestone 1 (compile `libslic3r_gui`) GREEN @ run 46; milestone 2 (link `OrcaSlicer` app) implemented via patches 0316-0318 and dispatched. Launch+screenshot follows once link is green. |
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

═══════════════════════════════════════════════════════════════════════
## ✅ MILESTONE: libslic3r_gui COMPILES for iPad — run 46 GREEN (2026-07-07)
═══════════════════════════════════════════════════════════════════════
ios-step3-gui.yml passed all 8 stages on commit 73af5ee. Every GUI
translation unit (~652 targets) compiles for the iPhoneSimulator 18.5 SDK
against the step-1 deps + patched wx iPhone port. This is the end of the
"step-3 compile" phase the prior handoffs were grinding on.

Fix arc this session (failing-file count per run):
  36:22 -> [wx-build fixes 37-41] -> 42:236* -> 43:5 -> 44:2 -> 45:1 -> 46:0
  (*the 236 was a single missing shipped header included everywhere -
   wx/osx/webviewhistoryitem_webkit.h - not 236 distinct problems.)

Patch stack now: step2 0201-0204, step3 0301-0315 (0303-0305 skipped in my
sparse verify only because they touch deps_src/libvgcode/glad paths outside
the GUI sparse-checkout; they still apply in CI). All verified applying to
pristine upstream; every edited #if chain checked for balance + branch
selection; wx-side flags validated through the real chkconf chain with the
local probe (now SDK-faithful via the TargetConditionals/OSAtomic stubs).

═══════════════════════════════════════════════════════════════════════
## ▶️ LINK STAGE IMPLEMENTED + DISPATCHED (2026-07-07, milestone 2)
═══════════════════════════════════════════════════════════════════════
Added patches 0316-0318 and switched the [7/8] step target from
`ninja libslic3r_gui` to `ninja OrcaSlicer` (compile milestone kept first,
then link). All 26 patches (step1 + step3 0301-0318) verified applying
cleanly to a fresh pristine clone.

- 0316-ios-platform-stubs: new file src/slic3r/GUI/ios_platform_stubs.cpp
  (plain C++, NO Objective-C/UIKit - lowest link risk) + a
  `if (CMAKE_SYSTEM_NAME STREQUAL "iOS")` block in src/slic3r/CMakeLists.txt
  that appends it. Provides iOS definitions for EVERY symbol from the 8
  excluded .mm files (all declared `#if __APPLE__`, so referenced on iOS):
    * RetinaHelper ctor/dtor/set_use_retina/get_use_retina/get_scale_factor
      (stashes wxWindow* in m_self; scale = wx GetContentScaleFactor()).
    * MacDarkMode's 10 fns (mac_dark_mode->false, mac_max_scaling_factor->
      2.0, WKWebView_*/set_*/initGestures/openFolderForFile/StaticGroup_
      layoutBadge -> no-ops).
    * InstanceCheck: send_message_mac[_closing] + OtherInstanceMessageHandler
      register/unregister/bring_forward -> no-ops (m_impl_osx=nullptr).
    * RemovableDriveManager register/unregister/list_devices/eject_device
      -> no-ops.
    * Mouse3DController init/shutdown -> no-ops (handle_input(DataPacketAxis)
      is in the .cpp, NOT stubbed; init/shutdown for non-Apple are `#else`
      in the .cpp so iOS needs them).
    * register_mac_deep_link_handler -> no-op.
    * dataview_remove_insets / staticbox_remove_margin -> no-ops;
      is_debugger_present->false (its only caller in GUI_App.cpp is inside a
      /* */ comment, but defined anyway - harmless).
    * wxMediaCtrl2 (the `#ifdef __WXMAC__` branch -> `: public wxWindow`):
      ctor (base wxWindow only, inert), dtor, Load/Play/Stop/SetIdleImage/
      GetState/GetVideoSize. DoSetSize is in MediaPlayCtrl.cpp (compiled),
      GetLastError is inline - both skipped.
    * CRITICAL: `wxDEFINE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent)` was
      defined ONLY in the two excluded wxMediaCtrl2 files -> redefined here.
  RetinaHelper is NOT duplicated: GLCanvas3D.cpp's copy is `#ifdef __WXGTK3__`
  only.
- 0317-orca-main-gate-glfw-ios: src/OrcaSlicer.cpp. Defines ORCA_IOS
  (= `__APPLE__ && !TARGET_OS_OSX`) and gates the GLFW include, glfw_callback,
  the CLI-thumbnail GLFW init block, and glfwTerminate behind `#if !ORCA_IOS`.
  `bool thumbnail_opengl_ready=false` hoisted OUT of the guard so the existing
  `if (!thumbnail_opengl_ready){...}` skips thumbnail render gracefully on iOS.
  A plain launch (no CLI actions) takes the `start_gui` path -> GUI_Run and
  never touches GLFW. #if/#endif balance verified (4 opens = 2 commented + 2
  bare closes).
- 0318-ios-app-link: src/CMakeLists.txt. The OrcaSlicer exe's APPLE link block
  linked IOKit/AVFoundation/AVKit/CoreMedia/VideoToolbox/OpenGL (wrong for
  iOS). Added `if (APPLE AND iOS)` using the PROVEN step-2 wx iOS link set
  (UIKit/OpenGLES/GLKit/QuartzCore/CoreGraphics/CoreText/CoreFoundation/
  Foundation/Security/AudioToolbox/CFNetwork/MobileCoreServices + -lz/-liconv/
  -lexpat/-llzma/-lc++); `elseif (APPLE)` keeps macOS. Also added an empty
  `elseif (APPLE AND iOS)` in the SLIC3R_GUI block so the desktop
  `-framework OpenGL` is skipped on iOS.

── WHAT'S NOT DONE (next agent starts here) ─────────────────────────────
FIRST: read ci-logs for the dispatched run (newest step3-run-N). This is the
first LINK attempt AND the first warm-ccache run (verify wall-time speedup).
The 8-file sweep may not catch every `#if __APPLE__`-guarded symbol - the
linker will surface any others in SUMMARY.txt's "undefined symbols (linker)"
section (grep now captures `"sym", referenced from`). Iterate: add missing
defs to ios_platform_stubs.cpp (regen 0316 from pristine base + re-run
acceptance test), repeat until link is green.

Known runtime item still open (compiles fine, matters at launch):
- Backendless webview: wxWebView::New() returns NULL on iOS. Confirm Orca's
  WebView::CreateWebView tolerates NULL (or guard callers) BEFORE the launch
  milestone, else GUI_Run may crash creating a web panel.

Once link is GREEN (follow-up milestone in same workflow):
A. Add simulator launch + screenshot to ios-step3-gui.yml: assemble a minimal
   Payload/OrcaSlicer.app (binary orca-slicer + Info.plist w/ CFBundleExecutable,
   CFBundleIdentifier, UIDeviceFamily=[2], LSRequiresIPhoneOS, a
   UILaunchStoryboard or launch-image key), `xcrun simctl create` an
   "iPad Pro 13-inch (M4)" (16GB) device, boot, install, launch, then
   `xcrun simctl io <udid> screenshot out.png` -> upload as artifact = STEP 3
   COMPLETE. (Orca needs its resources/ dir; symlink or copy into the .app.)
B. STEP 4: iphoneos SDK build + Payload/OrcaSlicer.app + zip -> unsigned .ipa
   -> GitHub Release.
C. STEP 5: real WKWebView backend (class exists on iOS, revival sites marked
   !defined(__WXOSX_IPHONE__)); AVPlayer camera in wxMediaCtrl2; UIDocumentPicker
   export in RemovableDriveManager.

── DEFERRED-TO-STEP-5 SITES ALREADY MARKED IN SOURCE ───────────────────
- wxWebView: built backendless (all wxUSE_WEBVIEW_* = 0; New()->NULL).
  Real headers compile; Widgets/WebView.cpp WKWebView revival sites are
  guarded !defined(__WXOSX_IPHONE__) with "step 5" comments.
- wxAnimationCtrl: ENABLED (generic) - SyncAmsInfoDialog spinner works.
- wxMediaCtrl2: neither .mm nor .cpp compiled on iOS; header declaration
  only. Camera video = step 5.
- Serial/IOKit, SendSystemInfo/IORegistry, CloudAgent/gethostuuid,
  Troubleshoot/CGDisplay: all guarded to macOS with graceful iOS
  fallbacks (empty id / hostname / skipped enumeration).

── INFRA STATE ──────────────────────────────────────────────────────────
- ccache is live in both step3 workflows (issue #2). Run 46 populated the
  cache under key ccache-step3-<run_id>; next run restores it via the
  ccache-step3- prefix and should recompile only changed TUs. VERIFY the
  speedup on the next iteration and report actual wall time - this is the
  first run with a warm ccache available, so the hypothesis (28min ->
  minutes) is now testable.
- WX_KEY salted to v2 and hashes all overlay files recursively incl. the
  file list. wx-prefix now correctly caches the fully-patched wx +
  shipped private/webview/osx headers.
- Log excerpt cap 400KB; SUMMARY "error signatures" grep fixed (both were
  silently broken before). ci-logs newest-dir still via
  GET /commits?path=ci-logs&per_page=1.

═══════════════════════════════════════════════════════════════════════
## SESSION UPDATE (2026-08-03) — wx regression fixed, runtime bugs
## pre-empted, device deps built, sideloadable IPA track added
═══════════════════════════════════════════════════════════════════════

### Where runs 49-51 actually were
Runs 49, 50 and 51 all died in the **wx build phase** — no link was ever
attempted after run 48. Commit 2f45ea2 (the fix for run 48's 351 undefined
symbols) un-guarded `src/osx/combobox_osx.cpp` for the iPhone port but left the
matching declarations in `include/wx/osx/combobox.h` behind `#if wxOSX_USE_COCOA`,
so `GetComboPeer`/`Popup`/`Dismiss` matched no declaration: 13 errors, one file,
and `-k 0` proves it was the *only* failing file.

**Run 48 remains the reference for the link stage**: 351 undefined symbols, ONE
failing target (the final executable), everything else compiling. Grouped by
class those 351 are 8 families, not 351 problems:
  wxGrid ~175 | wxGenericListCtrl 57 | wxComboBox 42 | wxAui 27 |
  dataobject/DnD/clipboard ~23 | wxWidgetImpl peers 7 | wxToolTip 6 |
  wxLogWindow 2
2f45ea2's cmake feature flags cover seven of the eight. The eighth,
`wxUSE_LOGWINDOW`, was missing from that flag list and is now passed.

### KEY INSIGHT for whoever continues this
`wxUSE_*` values come from the **cmake-option-driven generated setup.h**, not
from the checked-in `chkconf.h` edits in patch 0202 — that is why 0202 alone
left grid/listctrl/aui/combobox compiling to empty objects. Any wx feature Orca
needs must ALSO be passed as `-DwxUSE_<FEATURE>=ON` in the workflow's wx
configure step. Check `build/cmake/options.cmake` for whether a flag is a real
cmake option before assuming a chkconf edit is enough.

### Patches added this session
- step2/**0205** iphone-combobox-decls — extends the two `#if wxOSX_USE_COCOA`
  guards in `include/wx/osx/combobox.h` to include `wxOSX_USE_IPHONE`.
  `wxComboBoxBase` declares Popup/Dismiss as unconditional virtuals, so the
  `override` resolves. ALSO fixes a latent null-deref: `combobox_osx.cpp` reaches
  its peer via `dynamic_cast<wxComboWidgetImpl*>(GetPeer())` and dereferences it
  unchecked, but `extra_peers.mm`'s `CreateComboBox` returned a plain
  `wxWidgetIPhoneImpl`, which is not a `wxComboWidgetImpl` — every populated
  combobox would have crashed. `extra_peers.mm` now defines
  `wxIPhoneComboBoxPeer` deriving from both (mirroring the Cocoa
  `wxNSComboBoxControl`) and stores items in a `wxArrayString`. NOTE the port's
  own iphone `wxNSComboBoxControl` is dead code inside `#if 0` (it wraps
  NSComboBox, which does not exist on iOS) — do not try to revive it.
- step2/**0206** glcanvas-es3-depth — TWO runtime-only defects:
  (a) `WXGLCreateContext` pinned every canvas to `kEAGLRenderingAPIOpenGLES1`.
      ES1 has no shader entry points, but Orca renders GLES2/3 GLSL under
      SLIC3R_OPENGL_ES. This LINKS FINE (the OpenGLES framework exports ES1/2/3
      and 0305 removed glad's runtime loader on Apple) and fails only at runtime.
      Now ES3 with ES2/ES1 fallback.
  (b) GLKView defaults to `GLKViewDrawableDepthFormatNone` — a 3D scene would
      draw with no occlusion. Depth24 + Stencil8 requested at view creation.
- step3/**0319** webview-null-guards — wx is backendless on iOS so
  `WebView::CreateWebView` returns NULL. 10 of its 14 call sites check; 4 did
  not. The critical one is a **guaranteed startup crash**:
  `MainFrame::init_tabpanel` unconditionally builds MonitorPanel -> StatusPanel,
  whose ctor dereferences the null view immediately. Also guarded
  UpdateVersionDialog::CreateTipView and MarkdownTip's `topsizer->Add` (wxSizer
  asserts on a null window). MarkdownTip/PrivacyUpdateDialog's own CreateTipView
  bodies bind on `this`, not the view, so they were already safe.
- step3/**0320** ios-resources-dir — `CLI::setup`'s `__APPLE__` branch walks
  `parent_path().parent_path()/"Resources"`, right for `.app/Contents/MacOS/` but
  an iOS bundle is FLAT (`OrcaSlicer.app/OrcaSlicer`), so it resolved outside the
  bundle. Uses `parent_path()/"resources"` under ORCA_IOS (macro from 0317).
  Must sort after 0317; both patch OrcaSlicer.cpp.
- step3/**0321** ios-create-data-dir-parents — an iOS sandbox provisions only
  Documents, Library/Caches, Library/Preferences and tmp, so the
  `Library/Application Support` parent of the data dir is missing and boost's
  single-level `create_directory` throws at startup. Now `create_directories`.

All 21 in-scope patches verified applying in order to a pristine checkout.
(6 more apply in CI only — they touch deps/, libvgcode and glad paths outside a
GUI sparse checkout.)

### Verified NOT broken (checked, do not re-investigate)
- Entry path is sound: `main()` -> CLI::run -> `GUI_Run` -> `wxEntry` ->
  `wxGUIEventLoop::OSXDoRun` -> `UIApplicationMain(@"wxAppDelegate")`
  (`src/osx/iphone/evtloop.mm:87`). Same path the step-2 smoke app launched on.
- 0318's link block uses `CMAKE_SYSTEM_NAME STREQUAL "iOS"`, not a bare `iOS`
  variable, so it does not silently fall through to the macOS framework list.
- The portable-mode `data_dir` probe next to the executable fails harmlessly on
  iOS rather than crashing.

### Milestone 3 added to ios-step3-gui.yml
After the link, the workflow now assembles OrcaSlicer.app (binary + resources +
Info.plist), boots an iPad Pro 13-inch simulator, installs, launches and
screenshots. The DIAGNOSTICS are the point, not the screenshot:
`simctl launch --console-pty` captures stdout/stderr (Orca's boost log, wx
asserts), DiagnosticReports crash reports name the failing frame, and
`log show` gives the process log — all uploaded and copied to ci-logs/ on
failure. Launch is backgrounded and killed rather than wrapped in `timeout`,
which macOS does not ship.

### NEW: ios-step4-device-ipa.yml — the device (iphoneos) build
Device counterpart of step 3, ending in an unsigned sideloadable
`OrcaSlicer-iPad.ipa` published to a GitHub Release. **The whole dependency
stack built successfully for iphoneos on its first run** and is cached under
`ios-deps-device-v1-` — that was the multi-hour long pole and it is now done, so
later device runs restore it. The step-1 patches already branched device vs
simulator (OpenSSL `ios64-xcrun`, GMP/MPFR `-miphoneos-version-min`), which is
why this needed no new porting work. Packaging locates the executable by Mach-O
type rather than name, asserts `LC_BUILD_VERSION platform == IOS` (not
IOSSIMULATOR — the classic silent "installs nowhere" bug) and ships resources/
inside the bundle.

### NEW: ios-device-ipa.yml — a working sideloadable iPad app, today
Independent of the step-3 patch stack: builds **stock unpatched wx** and ships
two IPAs. `ipad-app/` is a native STL viewer (loads .stl from the Files-app
folder, renders with lighting, reports triangle count and bounding box in mm);
`smoke-app/` is the minimal wx proof. Both verified as arm64 Mach-O with
`platform = IOS`, unsigned, correct Payload/ layout.
Released at tag `ipad-app-run2`. Because it uses stock wx it keeps its ES1
fixed-function context — 0206 only affects patched (step 3/4) builds.
NOTE these two workflows trigger on **push** with path filters, because
`workflow_dispatch` only registers for workflows already on the default branch
and these are not on main yet.

### Where to pick up
1. Read the newest `ci-logs/step3-run-N/SUMMARY.txt` — "undefined symbol count"
   is the number that matters. Families, not individual symbols.
2. When the link is green, milestone 3 runs automatically; read
   `sim-launch.log` + the crash report before guessing at runtime causes.
3. Then dispatch step 4 (deps + wx now cached) for the device IPA.
4. Still deferred to step 5: the real WKWebView backend (revival sites are
   marked `!defined(__WXOSX_IPHONE__)`), AVPlayer camera, UIDocumentPicker export.

═══════════════════════════════════════════════════════════════════════
## SESSION UPDATE (2026-08-04) — link closed out, and why the GUI would
## not have rendered even once the link went green
═══════════════════════════════════════════════════════════════════════

Branch note: work continues on `claude/step-4-ipa-parity-bkdym6`, branched
from `claude/basic-ipad-app-plan-pb0b4p` at faaa588 so nothing is lost.
`ios-step4-device-ipa.yml` now triggers on pushes to both branches (it has
no usable `workflow_dispatch` — the file is not on the default branch yet).
`ios-step3-gui.yml` IS on main, so `workflow_dispatch` with `ref=<branch>`
works and runs the branch's copy of the file.

### The link: 2 symbols, closed (patch 0325)
Run 55 (simulator) and run 7 (device) both stopped at the same place:
`_gladLoaderLoadGLES2` / `_gladLoaderUnloadGLES2`, referenced from
libvgcode's `OpenGLWrapper`. 0323 had removed `glad/src/gles2.c` from
libvgcode because it defines the same `glad_gl*` entry points as the shared
`src/glad` target (263 duplicate symbols), which left the two loader
functions undefined. There is nothing for them to do on iOS anyway —
OpenGLES.framework is linked directly and 0324 already resolves every entry
point out of the process image — so 0325 skips them there. Branch selection
verified with a preprocessor trace; the macOS and non-Apple ES
configurations are byte-identical to upstream.

### ▲ THE BIG ONE: `SLIC3R_OPENGL_ES` was never a preprocessor macro (0327)
`SLIC3R_OPENGL_ES` exists **only as a CMake variable**, read by exactly one
file — `src/libvgcode/CMakeLists.txt`, which turns it into
`ENABLE_OPENGL_ES` for libvgcode's own sources. Nothing ever defined it for
the GUI. Confirmed against a real CI compile line for `libslic3r_gui`
(ci-logs/step3-run-11): the define is simply not there.

But `src/slic3r/GUI` writes its ES branches as `#if SLIC3R_OPENGL_ES`,
which with the macro undefined is `#if 0`. So **every iOS build so far
compiled the desktop OpenGL path**. What that costs:

- `GLShadersManager::init()` picks `"110/"` or `"140/"` instead of `"ES/"`.
  An ES 3.0 context reports version 3.0, so it lands on `110/` — desktop
  GLSL that no GLES driver will compile. Every shader fails, the 3D scene
  never draws, and the 38-file `orca-overlay/resources/shaders/ES/` set this
  repo ships would never have been opened.
- All the `#if !SLIC3R_OPENGL_ES` desktop-only fallbacks stay live.

0327 defines it on `libslic3r_gui` when the option is on. Audited what that
newly compiles: 15 regions, every one a single
`get_shader("dashed_lines")` / `get_shader("wireframe")` line plus the
prefix block. No new symbols, no compile risk.

The ES shader set was checked against the `140/` reference: all 38 are
`#version 300 es`, and every uniform and attribute present in `140/` is
present in the ES rewrite. `dashed_lines`/`wireframe` have no `140/`
counterpart because they are ES-only (desktop uses geometry shaders).

### ▲ 140 calls into entry points OpenGL ES does not have (0328)
Method: extract every `gl*(` call in `src/slic3r`, extract the entry points
glad's GLES2 header declares, diff, then evaluate each site's `#if` nesting
for `SLIC3R_OPENGL_ES=1`. Result: **140 live calls the ES driver has no
symbol for** — and glad leaves each slot null on iOS, so a call through one
jumps to address zero.

  PartPlate 67 | SkipPartCanvas 58 | 3DScene 5 | GLCanvas3D 3 | gizmos 6 |
  GLTexture 1

The one that matters most: `glClearDepth` in `GLCanvas3D::init()`. That runs
before the first frame is ever drawn, so this was a **certain crash on the
way to the first render**, independent of everything else. Turning on 0327
does not help — it fences off only 12 of the 140.

`src/slic3r/GUI/ios_gl_compat.cpp` (new, added to the iOS block 0316
created) fills the null slots right after the loader runs, called from
`OpenGLManager::init_gl`:
- ES equivalents where they exist: `glClearDepth` → `glClearDepthf`, the
  `*EXT` framebuffer names → the core entry points ES 3.0 promoted them to.
- No-ops otherwise: the fixed-function matrix stack, immediate mode,
  `glPushAttrib`/`glPopAttrib`, `glLineStipple`, `glPolygonMode`,
  `glTexEnvi`, client-state leftovers. None of that means anything against
  the shader pipeline Orca actually renders with; losing the drawing is the
  right outcome, crashing is not.
- Everything resolves via `dlsym(RTLD_DEFAULT, …)`, **not** glad's slots:
  glad only loads entry points up to the version the context reports, and an
  ES 3.0 context reports 3.0 — `glClearDepthf` is GL 4.1 in the desktop
  header and is never loaded.
- Verified the 29 installed functions are exactly the live-and-missing set:
  no gaps, no dead entries. Compiles clean in both configurations.

0328 also forces `s_framebuffers_type = Arb` on iOS. FBOs are core in ES
3.0, but no ES driver advertises `ARB_framebuffer_object` or
`EXT_framebuffer_object`, so detection landed on `Unknown` — and 3DScene's
outline pass treats anything that is not `Arb` as "use the EXT entry
points".

### Two more runtime fixes prepared the same way
- **0326** — MainFrame's hardcoded `SetSize(FromDIP(1200), FromDIP(800))`.
  A wx top-level window is a UIWindow and a UIWindow is the whole display,
  so that leaves dead space on a 13-inch iPad (1376x1032 pt) and would clip
  the sidebar off a smaller one. `FromDIP` is the identity here
  (`wxHAS_DPI_INDEPENDENT_PIXELS` is set for `__WXOSX__`), so the number is
  literal points. Now takes `wxDisplay`'s client area — which resolves on
  the iPhone port via `wxDisplayFactorySingleiOS` in `src/osx/iphone/utils.mm`
  — and clamps the desktop 76x49 em minimum to it.
- **step2/0208** — `wxAppDelegate` ran `OSXOnDidFinishLaunching()`, and so
  the app's `OnInit()`, **synchronously inside
  `applicationDidFinishLaunching:`**. iOS watchdogs that at roughly twenty
  seconds (0x8badf00d) and Orca's `on_init_inner` loads the whole preset
  bundle and builds MainFrame before returning. **The simulator does not run
  the watchdog at all**, so this is invisible in step 3 and only bites the
  device IPA. Now dispatched to the next main-runloop turn. Nothing on the
  iPhone port depended on the old ordering: `m_inited`/`m_onInitResult` are
  written only by the Cocoa `CallOnInit` and read only by Cocoa call sites,
  and the iPhone `wxApp::CallOnInit` is a no-op returning true.

### Startup path verified sound (do not re-investigate)
- `wxApp::CallOnInit` on the iPhone port is a no-op; `OnInit()` is reached
  through `OSXOnDidFinishLaunching()` (`src/osx/carbon/app.cpp:216`,
  `#if wxOSX_USE_IPHONE`). It IS called — just late, hence 0208.
- `instance_check()` is safe on iOS: `get_lock()` returns false on a fresh
  lock file, and `*cla.should_send` is only evaluated when it returns true.
- `set_miniaturizable` / `set_title_colour_after_set_title` /
  `set_tag_when_enter_full_screen` are all stubbed by 0316 (they are behind
  `#ifdef __WXOSX__`, which is defined for the iPhone port too).
- libvgcode's `glTexBuffer`/`glMapBuffer` sites are in the `#else` of
  `ENABLE_OPENGL_ES` — dead on iOS.
- `ENABLE_OPENGL_ES` appears in no libvgcode *public* header, and 0304's
  `Vec4` addition to `Types.hpp` is unguarded, so there is no ODR hazard
  from `src/slic3r` including `libvgcode/include/Types.hpp` without it.
- `resources/` is 242 MB; that is what ships inside the .app.

### CI changes
Step 3 now publishes `ci-logs/` on `always()`, not just on failure — once
the link is green a crashed app still leaves the job green, and Actions
artifact downloads are not reachable from the dev environment. The launch
console, crash report, os_log excerpt and the **simulator screenshot PNG**
now land in the repo where they can actually be read. The step also reports
a launch verdict (process still present in the simulator + crash-report
count) instead of trusting `simctl launch`'s exit code, which only says the
process was spawned. Both workflows take the executable from the link rule's
known output path before falling back to the Mach-O scan.

### ⚠ Two corrections to long-standing assumptions in this document

**1. Actions job logs are PARTIALLY readable — not a live tail.** The top of
this file says log downloads are blocked. The GitHub MCP server's
`get_job_logs` (`return_content: true`, plus `tail_lines`) does return log text,
so that is too absolute. But measured against two in-progress jobs it is not a
substitute for `ci-logs/`:

- step-3 run 57's job returned ~23 KB and then returned **byte-identical
  content on a later call** — a snapshot taken when the log was first flushed,
  not a tail that advances.
- step-4 run 10's job returned **HTTP 404** for the same call at the same time.

So: useful for a one-shot look at a job that has been running a while, useless
for watching progress, and not guaranteed to answer at all. It is worth trying
once when a run is stuck, and worth nothing as a polling mechanism. `ci-logs/`
remains the mechanism to rely on — it also survives log expiry and is greppable.

(The cache-miss finding below still holds: that snapshot showed Boost
*configuring and installing from source* seven minutes into the deps step, which
cannot happen on a cache hit. The diagnosis was sound; the claim that the
endpoint gives live progress was not.)

**2. The deps cache is being evicted between runs, and that is the long pole.**
`[3/8] Restore deps cache` completing in about a second, followed by Boost
configuring and installing from source, means a miss — the full dependency stack
is rebuilding, which is the multi-hour part of every run.

Cause: both step-3 and step-4 save a **new cache entry on every run**
(`ios-deps-v1-step3-${{ github.run_number }}`,
`ios-deps-device-v1-${{ github.run_number }}`). Each is multiple GB, covering
`orca/deps/build` + `ios-prefix` + `~/dep-dl`. A repository's total cache quota
is 10 GB, so two runs of each workflow are enough to push everything else out by
LRU — including the entry the next run needs. The `restore-keys` prefix then
finds nothing and the stack rebuilds from scratch.

Fix to make before the next round (NOT pushed yet, deliberately — editing
`ios-step4-device-ipa.yml` is itself a push trigger, and doing it mid-run would
start a redundant build): key the deps cache on a hash of the inputs that
actually change it (`patches/step1/*` plus the dep list in the workflow) instead
of `github.run_number`. Repeated runs then reuse one entry rather than minting a
new multi-GB one each time; when the key already exists GitHub skips the save
with a warning rather than failing. If that is not enough, the next lever is to
stop caching `orca/deps/build` (object files for OCCT/OpenCV/OpenVDB dominate
it) and rely on `ios-prefix` plus the ExternalProject `*-install` stamps.

**Do not run more than one step-3 and one step-4 job at a time.** Five
concurrent jobs were in flight this session; each rebuilt the whole dependency
stack and each would have raced to save a multi-GB cache at the end, guaranteeing
eviction. Three were cancelled for exactly that reason.

### Cancelling a run mid-deps poisons the next run's cache
Run 11 restored a device deps cache in 45 s and then still spent **25 minutes**
building deps, where run 10 had spent 5 seconds. Cause: `if: always()` is true
for a **cancelled** job as well as a failed one, so the three runs cancelled
earlier in this session (56, 8, 9) each ran their "Save deps cache" step partway
through their dependency build and wrote a **partial** entry. A `restore-keys`
prefix lookup returns the *most recently created* match, and those partial
entries were newer than run 10's complete one — so run 11 restored a half-built
stack and had to finish it.

Two consequences worth keeping:
- Do not cancel a run that is inside its deps step. Let it fail on its own, or
  accept that the next run pays for the difference.
- The stable-key change helps here beyond the quota problem: an exact key hit is
  preferred over the prefix fallback, so once a complete entry exists under
  `ios-deps{,-device}-v1-<hash of patches/step1>` the partial ones stop being
  reachable. They still occupy quota until they age out.

═══════════════════════════════════════════════════════════════════════
## SESSION (2026-08-04, cont.) — compile closed out, parity tracks opened
═══════════════════════════════════════════════════════════════════════

### The compile burn-down is DONE
Step-3 run 57 and step-4 run 10 each built **652 of 653 targets** with
`ninja -k 0`, failing on the same single file. That is the whole compile
surface, on both the simulator and the device slice. 0330 fixed it.

### Patches added (0325-0333 step3, 0209-0211 step2)
- **0325** last two link symbols (GLES2 glad loader, not compiled on iOS).
- **0326** MainFrame sized to the display. A wx top-level window is a UIWindow
  and a UIWindow is the whole screen; the hardcoded 1200x800 left dead space.
- **0327 ▲** `SLIC3R_OPENGL_ES` was **never a preprocessor macro** — only a
  CMake variable read by src/libvgcode. So every iOS build compiled the desktop
  GL path, and GLShadersManager loaded the `110/` desktop GLSL set, which no
  GLES driver compiles. The ES/ shader overlay in this repo was dead weight.
- **0328 ▲** 140 live calls into entry points OpenGL ES does not have. Orca's
  GUI is written against the desktop `<glad/gl.h>`, so they compile and then
  resolve to null on iOS. `glClearDepth` in `GLCanvas3D::init()` runs before
  the first frame — a certain crash. `ios_gl_compat.cpp` fills the null slots
  with ES equivalents or no-ops. Re-derive with `tools/gl-es-gap-scan.py`.
- **0329** iOS has no window-system framebuffer at object 0: wxGLCanvas is a
  GLKView and GLKit's drawable has a non-zero name. Orca's offscreen passes
  restore with `glBindFramebuffer(..., 0)`, which unbinds the drawable for
  good — the scene stops appearing from the first selection onward.
- **0330** `viewport` redefinition GLGizmoMeasure; the `if (is_core_profile())`
  braces that scoped the first one are themselves inside `#if !SLIC3R_OPENGL_ES`.
- **0331** duplicate `wxMediaCtrl2::DoSetSize` (0316's stub vs MediaPlayCtrl.cpp).
- **0332/0333** WebKit.framework on the iOS link lines; Orca's webview call
  sites un-guarded; `ios_webview_support.mm` implements the two WKWebView
  helpers for real (evaluateJavaScript matters — Orca drives the embedded pages
  through it almost entirely).
- **step2/0209 ▲** desktop pointer input. `SetupMouseEvent` assigned every
  modifier `= 0` and hardcoded the button to left, so shift-drag, cmd-click and
  alt-drag were all dead, and there was no right/middle button, no scroll wheel
  and no hover. Keyboard was already correct.
- **step2/0210+0211** real WKWebView backend. The backend is only lightly
  AppKit-coupled (7 sites in 1365 lines, most already `#if !__WXOSX_IPHONE__`);
  it needed a wxWidgetIPhoneImpl peer, the print path guarded, and
  `OSXWebViewPtr`/`WKWebView` added to defs.h's **iPhone** branch — that
  typedef exists only in the Cocoa branch, and its absence made four targets
  fail with six error signatures that all traced back to one line.
- **clipboard** wxClipboard backed by UIPasteboard.

### CI infrastructure — this is the important part for velocity
- **`ios-wx-only.yml` (NEW)**: builds just the patched wx port, simulator and
  device in a parallel matrix, in **~5 minutes** (run 1: 4m51s). Every
  remaining parity track is wx-side. Iterate here, not in the 40-minute Orca
  build. Triggers on pushes touching `patches/step2/**` or `wx-overlay/**`.
  It caught the 0210 webview breakage 40 minutes before step-4 run 14 hit the
  identical error.
- **`ios-step3-fast.yml`**: already existed and was unused. Restores the deps
  and wx prefixes instead of rebuilding them. Now also ships orca-overlay,
  defaults to the OrcaSlicer target, and does the full simulator launch +
  screenshot, publishing to ci-logs on `always()`.
- **deps cache keys are now stable** (`hashFiles('patches/step1/*.patch')`
  rather than `github.run_number`). The old per-run keys minted a multi-GB
  entry every run against a 10 GB quota, evicting the entry the next run
  needed — every round paid for a full dependency rebuild. Step-3 run 58
  restored in 50s and topped up in 4s, versus 37 minutes cold.
- **Do not cancel a run inside its deps step.** `if: always()` is true for
  cancelled jobs too, so it saves a *partial* cache, and a restore-keys prefix
  lookup prefers the newest match. That cost run 11 25 minutes.
- WX_KEY salted to **v3**: the `wxUSE_LIBJPEG=sys` change lives in the
  workflows, not in a patch, so it does not move the key on its own.

### Still open
Control peers (radio/toggle/spin return inert UIView wrappers), document-picker
export, AVPlayer camera, and the immediate-mode drawing 0328 turned into no-ops
(PartPlate 67 calls, SkipPartCanvas 58 — likely visible build-plate geometry).

**And the big one: the app has still never launched.** Everything above is
compile-validated only. The step-3 fast workflow now carries the launch; that
is the next thing to get evidence from. Two items in 0209 cannot be settled
without real hardware: the sign of the wheel rotation, and whether the pan
recognizer ever steals a drag.

═══════════════════════════════════════════════════════════════════════
## SCOPE DECIDED WITH THE USER (2026-08-04)
═══════════════════════════════════════════════════════════════════════

Target hardware: **Bambu A1 in LAN-only mode.** That single fact reshapes the
remaining work.

### ⚠ THE BLOCKER NOBODY HAD NOTICED
Bambu LAN mode in Orca does **not** talk to the printer directly. Both paths go
through prebuilt closed-source binaries that Orca downloads per platform:
- printing: `NetworkAgent::connect_printer(dev_id, dev_ip, ...)` ->
  `BBLNetworkPlugin` (BambuNetworkEngine)
- camera: `BambuSource` (`src/slic3r/Utils/BBLNetworkPlugin.cpp`)

`GUI_App.cpp` picks between windows/windows_arm/macOS/Linux builds of these.
**There is no iOS build, and iOS cannot load a runtime-downloaded dylib** — it
would have to be bundled and signed, and we do not have the binaries.

So the app as it stands would slice, render and load profiles perfectly and be
**unable to reach the printer at all**. That is not a porting bug; it is a
dependency that does not exist for this platform.

The way through is a native LAN backend speaking the documented protocols
directly, bypassing the plugin:
- **MQTT over TLS, port 8883**, authenticated with device serial + LAN access
  code — status and print control
- **FTPS, port 990** — upload the sliced 3mf
Orca already links CURL and OpenSSL, so the pieces are in the tree. This is
substantial but legitimate work, not reverse-engineering a closed binary.

### Agreed scope
IN:
1. Get it launching (in flight)
2. **Native Bambu LAN backend** (MQTT + FTPS) — the big one
3. **Live control peers** — radio / toggle / spin / statbox / searchctrl render
   blank and do not respond; Preferences and several dialogs need them
4. PartPlate drawing dropped by 0328 — **decide after the first screenshot**;
   if the plate renders correctly those were dead paths and this is free

OUT (explicitly declined):
- Printer camera (would need the port-6000 stream reimplemented; dropped once
  the plugin dependency was understood)
- Files-app / document-picker export (wireless to printer only)
- Tooltips, dark-mode detection, drag-and-drop of files
