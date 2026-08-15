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

> **NOTE (2026-08-05): the table immediately below is stale.** It was written
> when step 3 was at run 46 and has not tracked the work since. The current
> state is at the bottom of this file, under
> "SESSION 2026-08-05: the startup SIGSEGV, and everything consolidated onto
> main". Read that first; the rest of this document is still accurate as
> background but not as status.

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

═══════════════════════════════════════════════════════════════════════
## DESIGN: native Bambu LAN backend (task #13) — scoped, not yet built
═══════════════════════════════════════════════════════════════════════

### The surface to satisfy is only six functions
`src/slic3r/Utils/NetworkAgent.hpp` is 120 methods wide, but LAN-only operation
touches just these (verified against the call sites in DeviceManager.cpp):

    set_on_local_connect_fn(OnLocalConnectedFn)   // connect result callback
    set_on_local_message_fn(OnMessageFn)          // inbound MQTT payloads
    connect_printer(dev_id, dev_ip, username, password, use_ssl)
    disconnect_printer()
    send_message_to_printer(dev_id, json_str, qos, flag)
    start_local_print(PrintParams, update_fn, cancel_fn)

DeviceManager.cpp:2412 is the live call: `m_agent->connect_printer(get_dev_id(),
get_dev_ip(), username, password, use_openssl)`. Everything else in the LAN flow
is Orca's own JSON on top of those.

### What has to be written
**No MQTT library exists in the tree.** deps_src/ has no mosquitto and no paho,
because the protocol lived inside Bambu's closed plugin. So:

1. **MQTT 3.1.1 client over TLS** (~500-600 lines). Connect to
   `mqtts://<dev_ip>:8883`, username `bblp`, password = the LAN access code from
   the printer's screen. Self-signed cert, so verification must be off. Needs:
   CONNECT/CONNACK, SUBSCRIBE to `device/<serial>/report`, PUBLISH to
   `device/<serial>/request`, PINGREQ/PINGRESP keepalive, DISCONNECT, the
   variable-length-integer packet framing, and a receive thread that feeds
   `set_on_local_message_fn`. OpenSSL is already linked (dep_OpenSSL) so a BIO
   over a socket is the natural transport.
2. **FTPS upload** (~80 lines). CURL is already linked. `ftps://<dev_ip>:990/`,
   implicit TLS, `CURLOPT_USE_SSL=CURLUSESSL_ALL`, verifypeer off, user `bblp`
   + access code. Upload the sliced 3mf to the printer's cache directory, then
   publish the print command over MQTT.
3. **Wiring** (~150 lines). On iOS, route those six NetworkAgent entry points to
   the native agent instead of the plugin, and stop
   `GUI_App::show_network_plugin_download_dialog` from prompting for a plugin
   that will never exist on this platform.

Roughly 800-1000 lines of new C++ plus several CI rounds to compile-validate.
This is the single largest remaining item and it is a real implementation job,
not a shim.

### Notes for whoever writes it
- The camera (TCP 6000) is explicitly OUT of scope — do not let it pull the
  BambuSource dependency back in.
- Printer discovery already works without the plugin: deps_src/mdns is compiled
  and Bambu printers advertise over mDNS, so `dev_ip` arrives without help.
- Keep it in `src/slic3r/Utils/` as plain C++ (no Objective-C) so it compiles for
  both slices and could later serve desktop builds that want a plugin-free path.
- Validate the MQTT framing against a real A1 early; the protocol is documented
  by community projects but the payload schema is Orca's own.

═══════════════════════════════════════════════════════════════════════
## ✅ BUILT: native Bambu LAN backend (task #13) — 2026-08-04
═══════════════════════════════════════════════════════════════════════

The design above is implemented. Sources live in
`orca-overlay/src/slic3r/Utils/` (shipped into the Orca tree by every
step-3/4 workflow's `cp -R orca-overlay/. .`), wiring is
`patches/step3/0335-ios-bambu-lan-agent.patch`.

### What it is
| File | What it does |
|---|---|
| `BambuLanMqtt.{hpp,cpp}` | MQTT 3.1.1 over TLS (OpenSSL). Codec + client: CONNECT/CONNACK, SUBSCRIBE/SUBACK, PUBLISH both ways, PUBACK for inbound QoS 1, PINGREQ keepalive, DISCONNECT, receive thread, dead-link detection. No Orca/wx/boost dependency. |
| `BambuLanFtps.{hpp,cpp}` | Implicit-TLS FTPS upload on 990 through libcurl, progress + cancel. |
| `BambuLanDiscovery.{hpp,cpp}` | SSDP listener on 1990/2021, emits exactly the JSON `DeviceManager::on_machine_alive()` parses. |
| `BambuLanPrintCommand.{hpp,cpp}` | The `project_file` command and its `file:///sdcard/<name>` URL. |
| `BambuLanPrinterAgent.{hpp,cpp}` | `IPrinterAgent` implementation, registered under the **same `"bbl"` id** as the plugin wrapper. |

Registering under `"bbl"` is the whole trick: `GUI_App::switch_printer_agent()`
picks that id for every Bambu-vendor preset, so DeviceManager, PrintJob,
Monitor and the Device tab are untouched. `switch_printer_agent()` runs at
startup via `load_current_presets()` -> `Tab::load_current_preset()` ->
`on_presets_changed()`, and `NetworkAgent::set_printer_agent()` re-applies
the cached callbacks, so the agent is live before the first connect.

### Decisions worth knowing
- **connect_printer is asynchronous.** It returns `BAMBU_NETWORK_SUCCESS`
  immediately and reports through `set_on_local_connect_fn`, exactly as the
  plugin did — `DeviceManager::set_selected_machine()` ignores the return
  value and calls it from the UI thread, so blocking would freeze the app for
  the TLS handshake. CONNACK 5 is passed through as the string `"5"`, which is
  the value `GUI_App` special-cases into "Incorrect password".
- **TLS verification is off.** The printer's certificate is self-signed with
  no name to match; the access code is the authenticator, as in Bambu's own
  client. `SSL_CTX_set_security_level(0)` goes with it because the firmware's
  cipher suites predate OpenSSL 3's defaults.
- **Upload goes to the FTP root, print URL is `file:///sdcard/<name>`.** The
  firmware exposes the SD card as the FTP root, and Orca's per-model config
  carries `"ftp_folder": "sdcard/"` for C11/C12/N1/N2S (empty for X1, where
  the same path works). Matches TFyre/bambu-farm, which is proven on X1/P1/A1.
- **Both spellings of `bed_leveling`/`bed_levelling` are sent**; firmware
  generations disagree and unknown keys are ignored.
- **Cloud calls return errors on purpose** (bind, ping_bind, cloud print with
  no LAN credentials). This is a LAN-only agent.
- `GUI_App::on_init_network()` forces `should_load_networking_plugin = false`
  on iOS, which switches off every plugin load attempt *and* every
  "install the network plug-in" dialog.
- **Discovery is a convenience, not a requirement.** Joining a multicast group
  needs an entitlement a sideloaded build does not have, so joins are best
  effort. The manual IP + access-code path (which `InputIpAddressDialog`
  already forces on Apple platforms) needs none of it.
- **`NSLocalNetworkUsageDescription` is mandatory** in the Info.plist or iOS
  14+ blocks every connection to the printer. It is in `lan-test-app/build.sh`;
  the step-4 IPA plist needs it too.

### How it was validated
`tools/bambu-lan/` holds a mock printer (`mock_printer.py`: TLS MQTT broker +
implicit-FTPS server + SSDP announcer) and `selftest.cpp`, which links the
shipping sources. `tools/bambu-lan/run-selftest.sh` builds and runs both:
**111/111 checks pass**, covering the codec (varints, partial reads,
malformed input, multi-packet TLS records), the print command JSON, SSDP
parsing, and a live round trip — wrong access code -> CONNACK 5, connect,
subscribe, pushed report, QoS-1 publish, keepalive, clean disconnect, then a
byte-verified FTPS upload and an auth-failure case. Run it on any host with
OpenSSL + libcurl headers; it needs no iOS.

`BambuLanPrinterAgent.cpp` and the patched `NetworkAgentFactory.cpp` were also
`-fsyntax-only` checked against the real Orca headers before pushing (needs
boost, eigen3, tbb, cereal, and a generated `libslic3r_version.h`).

### `lan-test-app/` — the fast device check
`BambuLAN.app` is a UIKit app that links the same four backend sources and
nothing else. `.github/workflows/ios-lan-test-ipa.yml` builds OpenSSL (68 s
cold, cached after) and curl for `iphoneos`, compiles the app, and publishes
an unsigned IPA to a Release — minutes, not the 40-minute Orca build. It
covers connect, push-all, get_version, home, jog X/Y/Z, extrude/retract,
unload, nozzle/bed temperature, part/aux fans, chamber light, print speed,
pause/resume/stop, raw gcode, raw JSON, SSDP scan, and (with curl) upload a
3mf and start it. Every payload was copied from `DeviceManager.cpp` /
`DeviceCore/Dev*.cpp`, so a green result there is a green result in Orca.

### ✅ CONFIRMED AGAINST A REAL PRINTER (2026-08-04)
The user ran `BambuLAN.ipa` against their Bambu A1 in LAN-only mode: it
connected and everything worked. So the parts that could only be argued from
documentation before are now observed facts on real hardware:
  - MQTT 3.1.1 over TLS on 8883 with `bblp` + the LAN access code
  - the `device/<serial>/report` subscription and the status push
  - the control commands (they are byte-for-byte what `DeviceManager.cpp`
    sends, so Orca's own buttons drive the same JSON)
  - iOS's local-network permission path with `NSLocalNetworkUsageDescription`
Nothing about the protocol is speculative any more.

### What is NOT done
- The print path (FTPS upload + `project_file`) had not been exercised at the
  time of that test. If a print ever refuses to start, the URL is the thing to
  check: "List SD card" in the test app shows where the FTP root really maps.
- Camera (TCP 6000) stays out of scope, as agreed.
- `start_local_print_with_record` is the plain LAN print (no cloud record) and
  does not call `wait_fn`, which would poll for a cloud job id that never
  arrives.

═══════════════════════════════════════════════════════════════════════
## ✅ ROOT CAUSE: "Missing bundle ID" was the resources directory
═══════════════════════════════════════════════════════════════════════

Every attempt to install OrcaSlicer.app - simulator and, almost certainly,
device - failed with

    Failed to get bundle ID from .../OrcaSlicer.app - Missing bundle ID

The bundle's `resources/` directory is the cause. macOS filesystems are
case-insensitive, so a top-level `resources` is `Resources` to CFBundle,
which is the marker of an **old-style bundle** whose `Info.plist` lives
*inside* that directory. Not finding one there, the installer reports a
missing bundle id - about a plist that is present, lints clean, and from
which `defaults read CFBundleIdentifier` returns the right answer.

**Proved, not guessed.** `.github/workflows/ios-sim-probe.yml` builds a
30-line UIKit app and installs one bundle variant per hypothesis, so a round
takes ~2 minutes instead of a 50-minute Orca build:

| round | variant | result |
|---|---|---|
| 1 | plist exactly as step 3 writes it, 2 files | OK |
| 1 | + CFBundleSupportedPlatforms/DTPlatformName | OK |
| 1 | binary plist | OK |
| 1 | same plist + 3000 resource files | **FAILED** |
| 2 | 100 files / 2 MB · 1000 / 20 MB · 3000 / 59 MB | **all FAILED** |
| 3 | directory named `resources` | **FAILED** |
| 3 | directory named `Resources` | **FAILED** |
| 3 | directory named `orca-resources` | OK |
| 3 | directory named `data` | OK |
| 3 | nested `share/resources` | OK |

Size, file count and depth are all irrelevant; only the top-level name is.

**Fix:** the directory is `orca-resources` everywhere - patch 0320
(`resources_dir()` at runtime), patch 0334 (`BIN_RESOURCES_DIR` at build
time) and the three workflows that assemble a bundle. Each of those
workflows now refuses to package a bundle containing a top-level
`resources`/`Resources`, so it cannot come back.

The step-4 comment blaming this error on a truncated zip extraction was
wrong; it has been corrected in place. Keep ios-sim-probe.yml - the next
"why won't it install" question is two minutes away instead of an hour.

### Build speed - where the time actually goes
Measured on fast run 4 (18:45:49 -> 19:39:16):

    checkout + patches + cache restores + configure    45 s
    ninja                                          50 m 34 s

so "fast" is entirely about ccache, and **nothing in either workflow has
ever printed a ccache statistic**. The circumstantial evidence says it was
not working: run 62 saved its ccache in 5 seconds and run 4 restored it in
4 - empty-cache numbers after 650 translation units. Both workflows now run
`ccache -z` before the build and publish `ccache -s -v` to ci-logs, and the
cache is sized for the job (3G, was 1.5G), with CCACHE_BASEDIR/NOHASHDIR so
an object's absolute path stays out of the hash and `system_headers` so the
SDK does not get hashed into every object.

**Caches are per-branch.** A run can only restore caches created on its own
branch or on the repository's default branch. That is why run 62 spent
21m52s "topping up" deps whose key had not changed: the entries existed, but
on sibling branches. Anything long-lived (deps prefix, wx prefix) should be
built once on `main` so every future branch inherits it - otherwise each new
branch pays ~28 minutes before it compiles a line of Orca.

═══════════════════════════════════════════════════════════════════════
## The simulator check was lying, and what it says now
═══════════════════════════════════════════════════════════════════════

The step-3 launch step used to do this:

    xcrun simctl launch --console-pty ... &
    sleep 60
    kill $LPID
    xcrun simctl io "$UDID" screenshot artifact/orca-on-ipad.png

Nothing in there asks whether the app is running. `simctl launch` returns 0
the moment the process is spawned, and `simctl io screenshot` photographs
whatever is on the screen - which, for a build that exits during startup, is
SpringBoard. Runs 5 and 6 were reported as "installs, launches, screenshot
attached" on that basis. The screenshot was of the home screen.

`tools/ci/sim-launch-verify.sh` replaces it, and both step-3 workflows call
it rather than carrying two copies:

* polls `simctl spawn <udid> launchctl list` once a second until the bundle
  id appears, up to 60 s, and records when it first did;
* takes a screenshot at t+2, 7, 17, 37 and 67 s **after** it appeared,
  re-checking aliveness before each one and stopping at the first checkpoint
  the app has gone from;
* photographs the home screen first (`00-home.png`) so a screenshot of
  nothing is recognisable as one;
* measures every PNG - resolution, distinct colours, dominant colour and its
  share - by decoding it in Python after `sips` shrinks it to 240 px, so the
  job can report whether anything was drawn without a human opening files;
* extracts Orca's own log from the app's data container (`orca-app-log.txt`),
  which is the only record of how far `on_init_inner()` got: the system log
  shows UIKit's half of the story and stops;
* **fails the step** when the app never appears, or appears and then exits.

A green step-3 job now means the app was still running 67 s after launch,
and `artifact/orca-on-ipad.png` is a picture taken while it was.

═══════════════════════════════════════════════════════════════════════
## ⛔ SESSION (2026-08-05) — THE ONE BUG THAT BLOCKS EVERYTHING
═══════════════════════════════════════════════════════════════════════

**Read this section first. It supersedes every earlier claim about why the
app "exits during startup".**

### What is actually wrong

A wxWidgets iOS app built against the cached wx prefix **dies before
`wxApp::OnInit()` is entered**. Not Orca specifically - any app.

Proved with `wx-probe/`, a ~350-line wx app that builds in **5 seconds**
against the cached prefix. It writes a marker from a static initialiser
(before `main`) and another on entry to `OnInit`, into
`$HOME/Documents/wxprobe.log`, which the launch script pulls out of the
simulator's app container. Run 5's log, complete:

    WXPROBE static init            ran

Nothing else. So: the binary loads, dyld is fine, static initialisers run,
and the process is gone about a second later - no crash report, no
termination message in the system log, which is what a clean exit looks
like.

**OrcaSlicer does exactly the same thing.** step3-fast run 8 published an
*empty* `orca-app-log.txt`: Orca never created its own log file, and
`set_log_path_and_level()` runs early in `init_app_config()`. It is not
getting far either.

This one bug is the whole distance between where the port is and a
screenshot of the UI. Everything else is built and working.

### The 4-minute loop that will find it

`.github/workflows/ios-wx-probe.yml` - restores the wx + deps caches,
compiles `wx-probe/main.cpp` (5 s), installs, launches, screenshots,
publishes to `ci-logs/wx-probe-run-N/`. **Do not debug this inside the Orca
build**; that is 55 minutes per question.

Triggered by pushing anything under `wx-probe/**`,
`tools/ci/sim-launch-verify.sh`, or the workflow file. (A workflow that
exists only on a feature branch cannot be dispatched through the API -
GitHub resolves the name against the default branch and 404s. Hence the
push trigger.)

The probe now also carries, and the next run will report:
* a marker in the `wxApp` constructor - was the object built at all;
* `OnAssertFailure` - a wx assertion on iOS cannot show its dialog, and the
  override deliberately does not call the base;
* `OnExceptionInMainLoop` / `OnUnhandledException`;
* `OnExit` - if this fires it is a clean shutdown, not a death;
* **a control**: `smoke-app/` (the step-2 proof: frame, button, green GL
  canvas) built from the *same* cached prefix. If the control dies too, the
  prefix regressed and the answer is in `patches/step2` / `wx-overlay`. If
  the control renders, the difference between the two apps is the answer.

### Where to look if the control renders

`wx-probe` differs from `smoke-app` in: `UIDeviceFamily [2]` vs `[1,2]`,
`UIRequiresFullScreen`, and it links `-framework WebKit` +
`UniformTypeIdentifiers` (`PROBE_BUILD=with-webview` - wxWebView **does**
compile and link for iOS, which is worth knowing on its own: Orca's
MainFrame builds web-view panels).

### ⚠ Two corrections to things this document used to say

1. **"The app exits during startup" was never established before today.**
   Runs 5 and 6 concluded that from `simctl spawn <udid> launchctl list`
   returning no match. That command **does not list app processes under
   their bundle id at all** - it answered "not running" for every app,
   always. wx probe run 4 proved it: host `ps` by pid, host `ps` by name,
   `pgrep`, and the simulator's own `launchctl list` (357 entries) were all
   asked at once and all agreed the app was gone, so the *conclusion*
   happens to hold - but it was luck, not evidence.

2. **The screenshots in runs 5 and 6 were of SpringBoard.** The old launch
   step shot the screen after killing the console reader, with no check that
   anything was running. `tools/ci/sim-launch-verify.sh` replaces it: polls
   the host pid `simctl launch` prints, shoots at t+2/7/17/37/67 s **while
   alive**, measures every PNG (size, distinct colours, dominant colour), and
   fails the step if the app never appears or goes away.

### Build speed: root cause found, fix in but unverified

> **VERIFIED (2026-08-10), and step 4 took the fallback.** The step-4 workflow
> builds with the PCH **off**, which is the fallback named at the end of this
> section, and it works: runs 96 and 99 report 635 `direct_cache_hit` against
> 1-3 misses out of ~636 objects. Nothing was exposed by removing the
> FORCEINCLUDE. The mtime-pinning approach in `ios-step3-fast.yml` was not what
> settled it.

ccache hits 76 of 639 objects, run after run. The 76 are exactly the
targets that do **not** use a precompiled header (deps_src, imgui,
libvgcode, libslic3r_cgal, the ObjC shims, `OrcaSlicer.cpp`). The 563
misses are exactly `libslic3r` + `libslic3r_gui`, the only two targets that
call `add_precompiled_header` (upstream `src/libslic3r/CMakeLists.txt:636`,
`src/slic3r/CMakeLists.txt:808`).

ccache hashes the `.pch` it is told to `-include`, and clang serialises the
**size and mtime of every header** that went into a `.pch`. Orca is
re-cloned every run, the wx overlay headers are re-copied every run, and
`configure_file` rewrites `libslic3r_version.h` every run - so the `.pch` is
byte-different every run even when no source changed.

`ios-step3-fast.yml` now pins those mtimes to a fixed instant (the orca
tree after patching, the wx + deps prefixes after the overlay copy, and the
generated headers in the build tree after configure - *only* the headers:
an mtime older than the CMakeLists would make ninja re-run cmake and undo
it). `pch-fingerprint.txt` is published each run; **compare two runs'
digests to confirm**. If they still differ, the fallback is
`-DSLIC3R_PCH=OFF`, which fixes the hashing by removing the input - at the
risk of exposing translation units that relied on the FORCEINCLUDE.

Expected payoff: ~55 min -> ~10 min per Orca iteration. Note the first run
after the change still misses everything (different `.pch` hash); the
*second* run is the fast one.

### The screenshot loop no longer needs a compile

`ios-step3-fast.yml` publishes the linked executable as artifact
`orca-binary`. `ios-relaunch.yml` downloads the newest one via the REST API
(newest-first, repo-wide), rebuilds the bundle around it - resources from a
shallow Orca clone plus the overlay, ~20 s - and runs the same verification.
**~4 minutes per launch experiment.** Bundle assembly lives in
`tools/ci/assemble-sim-bundle.sh` so the build workflow and the relaunch
workflow cannot drift into testing different bundles.

### Step 4 (device IPA) is GREEN

step4 run 24: device deps 21 min, wx 4 min, Orca build+link 31 min, **0
failed targets, 0 undefined symbols**, unsigned `OrcaSlicer-iPad.ipa`
assembled and uploaded as a workflow artifact. The job is marked failed only
because `gh release create` returned `HTTP 403: Resource not accessible by
integration` - the token a workflow_dispatch from an app integration gets
cannot always create a release, even though the same token pushed ci-logs
seconds earlier. That step is now non-fatal; **the IPA is the artifact**.

It also warmed the device deps + wx + ccache caches on this branch, so the
next device build is ~35 min rather than ~58.

### Patch 0336 (new this session)

`patches/step3/0336-ios-startup-no-modal-blockers.patch`:
* `on_init_inner()` raises a **parentless modal dialog** from `OnInit`, before
  UIApplication has an event loop, whenever `Http::tls_global_init()` cannot
  find a certificate store - which on iOS is always, there is no OpenSSL cert
  directory. `ShowModal` cannot run there; anything but `wxID_YES` makes
  `OnInit` return false. Answered in the affirmative on iOS and logged
  (`Http::priv` sets `CURLOPT_SSL_VERIFYPEER` to 0 on every request, so the
  store is never consulted).
* The splash screen is skipped on iOS - a second UIWindow driven by
  `wxYield()` from inside `OnInit`.

This is correct on its own merits but is **not** the bug above: the probe
dies before `OnInit`, and it contains none of this code.

### Order of work for the next session

1. Read `ci-logs/wx-probe-run-6/` (or the newest). The control tells you
   whether the wx prefix starts *any* app.
2. Fix that. It is the only thing between here and a UI screenshot.
3. Re-run `ios-step3-fast.yml`, confirm `pch-fingerprint.txt` matches the
   previous run's, and confirm the ccache hit rate jumps on the run after.
4. Use `ios-relaunch.yml` for every launch-behaviour question after that.
5. When a screenshot shows the UI, run `ios-step4-device-ipa.yml` and take
   the IPA from the workflow artifact.

Note on first launch: with no config, Orca runs the ConfigWizard
(`config_wizard_startup()` fires when `!m_app_conf_exists ||
only_default_printers()`). A screenshot of the wizard is still proof the GUI
renders; for a plater screenshot, pre-seed `OrcaSlicer.conf` in the data
container between install and launch.

### ▲ RESULT (wx probe run 6): the wx prefix is what regressed

Two facts, both from `ci-logs/wx-probe-run-6/`:

**1. The probe reaches the wxApp object and no further.**

    WXPROBE static init            ran
    WXPROBE wxApp ctor             ran

No `OnInit`, no `wx ASSERT`, no exception, no `OnExit`. So the process dies
between the `wxApp` object being constructed and `OnInit` being called -
inside wx's own app initialisation (`wxEntry` / `wxApp::Initialize` /
`CallOnInit`, or the UIApplicationMain delegate path), and it dies *without*
tripping wx's assert or exception machinery.

**2. The step-2 smoke app does not run either.** `smoke-app/` compiles clean
against the cached prefix (`smoke-build.log`: warnings only, "built:
.../WxSmoke.app") and then fails to start - `smoke-launch-verdict.txt` says
"first seen alive after: never", and `simctl launch` did not even print a
pid line.

That is the same `smoke-app/` whose screenshot - frame, button, green GL
canvas - is this document's step-2 proof. **So the regression is in the wx
prefix, not in Orca, not in the probe's widget choices, and not in patch
0336.**

#### Where to start

The prefix under test is `ios-wxprefix-v3-<hash of wx-overlay +
patches/step2>`. Something in `patches/step2/*` or `wx-overlay/` between the
green step-2 run and now stops a wx app starting. Prime suspects, in order:

* `wx-overlay/include/wx/osx/iphone/evtloop.h` and whatever
  `src/osx/iphone/evtloop.mm` does around line 87 - this document already
  notes that as "the same path the step-2 smoke app launched on", and an
  event loop that returns immediately is exactly a process that exits
  between `wxApp` construction and `OnInit` with no assert and no exception.
* `wx-overlay/src/osx/iphone/extra_peers.mm` / `extra_stubs.mm` - a stubbed
  peer that returns null during app init.
* `patches/step2/*` - diff against whatever produced the green step-2
  screenshot and bisect; each cycle is ~4 minutes through
  `ios-wx-probe.yml`, and the control is built into it.

`git log --oneline -- wx-overlay patches/step2` is the shortest path to the
candidate set.

#### One gap in the harness

The control's own launch log was not published in run 6 (only its verdict).
That is fixed - `smoke-sim-launch.log`, `smoke-sim-oslog.txt`,
`smoke-alive-methods.txt` and `smoke-orca-app-log.txt` are published from the
next run on. "pid none" in run 6's control verdict means `simctl launch`
printed no pid at all, which is a *different* failure from the probe's (which
gets a pid and then dies); the control's launch log will say which.

---

# SESSION 2026-08-05: the startup SIGSEGV, and everything consolidated onto main

Read this section before anything above it. It supersedes the status table at
the top of the file and corrects two conclusions the previous handoff drew.

## Headline

**The app now starts and renders.** The wx probe (run 17) reaches `OnInit`,
builds every widget class Orca depends on, draws through OpenGL ES, and stays
alive for the whole observation window:

    WXPROBE OnInit                 entered
    WXPROBE wxNotebook             constructed
    WXPROBE basic controls         constructed
    WXPROBE wxDataViewListCtrl     constructed + 6 rows
    WXPROBE wxGLCanvas             constructed
    WXPROBE wxWebView              created + SetPage
    WXPROBE frame Show()           returned
    WXPROBE OnInit                 returning true
    WXPROBE gl version             OpenGL ES 3.0 APPLE-23.0.2
    WXPROBE gl default fbo         1
    WXPROBE gl program link        ok
    WXPROBE gl draw                ok
    WXPROBE gl SwapBuffers         returned
    WXPROBE wxPaintDC              painted

    first seen alive after: 1 s
    still alive at t=14 s (last checkpoint 14s)
    screenshots taken while alive: 3

That is notebook + dataview + custom wxDC painting + GL-through-shaders +
WKWebView, all on iOS 26.2. The open question was never whether those widgets
work; it was whether any wx app could start at all.

## The bug: wxApp::OSXOnBuildMenu dereferences a null menu bar

`patches/step2/0213-guard-null-menubar-on-build-menu.patch`

wx, in `src/osx/carbon/app.cpp`:

    void wxApp::OSXOnBuildMenu(WX_NSObject builder)
    {
        wxMenuBar::MacGetInstalledMenuBar()->OSXOnBuildMenu(builder);
    }

`MacGetInstalledMenuBar()` returns `s_macInstalledMenuBar`, which is `nullptr`
until some `wxMenuBar` is installed. `wxMenuBar::OSXOnBuildMenu` then walks
`m_rootMenu->GetMenuItems()` through a null `this`.

UIKit rebuilds the main menu from an after-CA-commit block on the **first turn
of the run loop**, to populate the key-command table, and it does so whether or
not the app has any menus - before `OnInit` has run. So every wx iOS app was
segfaulting during startup: one that installs its menu bar in `OnInit` gets
there too late, and one with no menus never installs a bar at all.

The backtrace that settled it (probe run 16):

    2  wxMenuBar::OSXOnBuildMenu(NSObject*) + 376      <- fault
    3  wxApp::OSXOnBuildMenu(NSObject*)
    4  -[wxAppDelegate buildMenuWithBuilder:]
    5  -[UIMenuSystem _buildMenuWithBuilder:fromResponderChain:...]
    8  -[UIMainMenuSystem _automaticallyRebuildIfNeeded]
   11  -[UIApplication _immediatelyUpdateSerializableKeyCommands]
   12  -[_UIAfterCACommitBlock run]
   22  UIApplicationMain
   23  wxGUIEventLoop::OSXDoRun()

It became reachable when **patch 0202 enabled `wxUSE_MENUBAR`** for the iPhone
build - that flag is what compiles the `buildMenuWithBuilder:` delegate method
into existence. The flag has to stay: Orca's MainFrame needs the class.
Patch 0208's deferral of `OnInit` widens the window but does not cause it.

## Two corrections to the previous handoff

1. **"The wx prefix is what regressed - bisect `patches/step2` and
   `wx-overlay`" was wrong.** Nothing in the prefix regressed and the step-2
   smoke app's source never changed. It was one enabled feature flag meeting an
   unguarded pointer. The smoke app crashes identically to the probe, which is
   exactly what places the bug in wx rather than in Orca or in any widget
   choice. Do not spend runs bisecting step-2 patches.

2. **"crash reports: 0" was never a real measurement.**
   `sim-launch-verify.sh` counted crash reports matching `OrcaSlicer*`, so for
   `WxProbe`/`WxSmoke` it printed zero no matter what happened. Every earlier
   conclusion of the form "it dies without crashing" was read off a search that
   could only ever find nothing. Now fixed to match the executable under test.

## Unproven, and worth knowing

- **`-Wl,-u,_OBJC_CLASS_$_wxAppDelegate`** was added to the probe, the smoke app
  and OrcaSlicer's link (patch 0318) on the theory that ld drops the archive
  member defining wx's delegate, since `UIApplicationMain` names it only as a
  string. The class is definitely present now - but the `nm` check that
  originally reported it MISSING proved unreliable, so the flag's *necessity* is
  not established. It is harmless and defensible; do not treat it as a
  confirmed fix, and if something about it ever gets in the way, retest rather
  than assume.
- **The simulator runtime is iOS 26.2, while the SDK is 18.5.** The runner image
  moved without this repo changing a line, and `sim-launch-verify.sh` selects
  the newest installed runtime. The verdict file now records it. This is
  probably closer to a modern iPad than 18.5 would be, so it was deliberately
  not pinned backwards - but it is a variable that can change under you.
- **The rendered screenshot is 91% white** with 137 distinct colours
  (`ui-t14s.png`). Something real is drawn - a blank window would not have 137
  colours - but nobody has *looked* at it. Worth eyeballing
  `ci-logs/wx-probe-run-17/orca-on-ipad.png` before assuming the UI is correct.

## Everything is on main now

All three feature branches were merged; `main` contains every commit from
`claude/basic-ipad-app-plan-pb0b4p`, `claude/lan-backend-implementation-gxu2jr`
and `claude/step-4-ipa-parity-bkdym6`, verified by ancestry, per-file diff,
and no-op re-merge. Work continues on `main` (and the mirror branch
`claude/merge-branches-to-main-78ua1g`).

One merge conflict was resolved by hand, in `ios-step4-device-ipa.yml`: the LAN
branch had made `gh release create` non-fatal (a 403 was marking good builds as
failed) while the step-4 branch had added `OrcaSlicer-iPad-minimal.ipa` as a
second release asset. Both are kept - the minimal IPA is published inside the
403-tolerant wrapper.

## CI infrastructure changed this session

- **Caches now live on `main`.** Actions caches are readable only from the
  branch that wrote them and from the default branch, so everything built on a
  feature branch was invisible to `main`. The simulator deps (~31 min) and wx
  (~5-7 min) prefixes are now saved on `main`, which makes them readable from
  every branch. The **device** deps cache (`ios-deps-device-v1-*`) has *not*
  been rebuilt on main yet - the first step-4 run there pays for it.
- **`ios-wx-probe.yml` builds its own wx prefix when the cache misses**, instead
  of hard-failing. It used to be pinned to whichever branch last ran step 3.
- **Build failures publish immediately.** Job log downloads are blocked in this
  environment, so `ci-logs` is the only channel; routing it through the end of
  the job meant a cancelled run published nothing and a healthy one delayed the
  log behind the control step. There is now an `if: failure()` publish right
  after the build, with the **full** log (the interesting errors are from the
  first link attempt, at the top, which a `tail` cuts off) plus a grepped
  `build-errors.txt`.
- **Simulator steps have 14-minute timeouts.** One run wedged in `simctl` for 35
  minutes and had to be killed, which lost the log it would have published.
- **`sim-launch-verify.sh`** records the simulator runtime in the verdict,
  collects crash reports under the right name, and writes `crash-reports.txt`.
- **`wx-probe/probe_boot.mm`** is startup instrumentation linked into both the
  probe and the smoke app: it traces both launch callbacks through swizzled
  IMPs, logs whether `wxAppDelegate` is in the binary, installs
  exception/signal/atexit handlers, dumps a backtrace via
  `backtrace_symbols_fd` and re-raises so the OS still writes a `.ips`. It is
  optional in both builds (`probe_note_oninit` is declared `weak_import`) so a
  compile error in the diagnostic cannot take down the run it exists to
  explain. That mistake cost two rounds - do not re-couple them.

Gotchas paid for in this session, worth not rediscovering:

- The runner's `/bin/bash` is **3.2**, where expanding an empty array under
  `set -u` is a fatal "unbound variable".
- **`-ObjC` is not `-Wl,-ObjC`.** Bare `-ObjC` is the clang *driver's* language
  selector and fails outright next to `-std=c++17`.
- `-Wl,-ObjC` works but is far too broad: it force-loads every ObjC-bearing
  object in every wx archive, which drags in `webview_webkit.mm.o` and a wall of
  undefined WebKit symbols. `-u` names one symbol and loads one member.
- On Mach-O, `__attribute__((weak))` on a *declaration* is still an undefined
  symbol at link time. `weak_import` is the one that permits an absent
  definition.
- wx headers must be included **before** UIKit in a `.mm`: UIKit drags in
  `<AssertMacros.h>`, whose `check()`/`verify()` macros collide with wx, and wx
  suppresses them from `wxprec.h` - which only helps if wx comes first.

## Where step 3 and step 4 stand

- **Orca compiles and links clean** (step 3 run 62: 0 failed targets, 0
  undefined symbols). That predates this session and is unchanged.
- A step-3 simulator run was in flight during this session **without** patch
  0213. Expect it to link fine and then die at launch with the menu-bar
  SIGSEGV. That is not a new bug; re-run step 3 on current `main` to get a real
  Orca screenshot.
- **Step 4 (device IPA) is the live task.** Verified before dispatch that it
  applies `patches/step2/*` (so 0213 reaches the device wx build, line 154) and
  `patches/step1 + step3` (so 0318's link flag reaches the device Orca link,
  line 87). Adding 0213 changes `WX_KEY`, so the device wx prefix rebuilds once.
- Both known "Missing bundle ID" causes are merged and in the step-4 path: the
  `plutil -convert binary1` conversion, and the `orca-resources` rename (a
  top-level `resources` directory reads as `Resources` on a case-insensitive
  filesystem, and CFBundle then treats the flat bundle as an old-style one).
  **Neither has ever been verified together on a device build** - that is what
  the running step-4 job is for.

## Next steps

1. Read the step-4 result. If the IPA builds, the artifact is
   `orcaslicer-device-ipa`; `OrcaSlicer-iPad-minimal.ipa` is published alongside
   it as an install bisection tool (executable + Info.plist only, no resources).
2. Re-run `ios-step3-gui.yml` on current `main` for an Orca simulator
   screenshot now that 0213 is in. First launch runs the ConfigWizard
   (`config_wizard_startup()` fires when `!m_app_conf_exists ||
   only_default_printers()`); a wizard screenshot still proves the GUI renders.
   Pre-seed `OrcaSlicer.conf` in the data container between install and launch
   if a plater screenshot is wanted.
3. Sideload onto the device. This cannot be verified from CI - the simulator is
   as far as automation reaches.
4. Then parity work: the native Bambu LAN backend (MQTT + FTPS) is implemented
   and was confirmed against a real A1; camera/export remain stage 5.

## How to iterate quickly

`ios-wx-probe.yml` is the fast loop: ~3 minutes to build a wx app against the
cached prefix, ~13 minutes end to end including two simulator launches, and it
answers widget and startup questions without a 50-minute Orca build. It runs on
`main` now and builds its own prefix if the cache misses. Use it before
committing anything to a full Orca cycle.

# SESSION 2026-08-05 (later): the link fix, the first real IPA, and what the
# device actually does with it

This section supersedes the "Where step 3 and step 4 stand" list above.

## Step 4 is green. There is a real IPA.

`orcaslicer-ipa-run26` — `OrcaSlicer-iPad.ipa` (141 MB, `Payload/OrcaSlicer.app`
with a 123 MB arm64 iphoneos binary + `orca-resources`) and
`OrcaSlicer-iPad-minimal.ipa` (38 MB, executable + Info.plist only).
Verified in-job: bundle id `org.orca-ios.orcaslicer`, executable `OrcaSlicer`,
`vtool` platform IOS.

## Why run 25 failed: Ninja ate the '$' in the -u delegate symbol

    Undefined symbols for architecture arm64:
      "_OBJC_CLASS_", referenced from:
          <initial-undefines>

Patch 0318 passed `-Wl,-u,_OBJC_CLASS_$_wxAppDelegate` as an ordinary link
flag. CMake writes it into `build.ninja` **without escaping the `$`**, Ninja
reads `$_wxAppDelegate` as one of its own variable references and expands it to
nothing, and ld is handed `-u _OBJC_CLASS_`. Reproduced locally with cmake 3.28
+ ninja 1.11: `build.ninja` carries the literal `$` and `ninja -t commands`
prints `-Wl,-u,_OBJC_CLASS_`. `/bin/sh` would eat it too if Ninja ever stopped.

Fixed by passing the flag in a **clang response file** (`file(WRITE ...)` +
`target_link_options(... "@file")`): the driver reads the file itself, so the
contents pass through neither Ninja nor the shell. Verified with
`clang++ @rsp -###`.

Run 25 was the first Orca build ever to carry the `-u` form — step-3 run 63 was
still at `b169472` with `-Wl,-ObjC`, which is why it linked and step 4 did not.
**Never write a `$` into a link flag from CMake.** Same rule for the two
`build.sh` scripts, which already escape it because they go through `sh`.

## nm is settled-unreliable for the delegate; otool -ov is the right probe

The post-link check added this session took its second branch:

    + nm -a .../OrcaSlicer | grep -q '_OBJC_CLASS_\$_wxAppDelegate'   -> no match
    + otool -ov .../OrcaSlicer | grep -q wxAppDelegate                -> match
    wxAppDelegate: present (otool -ov)

`otool -ov` reads the Objective-C metadata — the class list the runtime itself
consults, which is what `UIApplicationMain` resolves the delegate name against.
That is the authoritative answer. Do not re-litigate this with `nm`.

## THE APP RUNS ON A REAL iPad

Confirmed by the user on an iPad16,5 running iPadOS 27.0 beta, sideloaded and
re-signed (`org.orca-ios.orcaslicer.9YHLT3UZJ6`): OrcaSlicer renders its actual
home screen — the Prepare / Preview / Device / Project tab bar, the Orca Cloud
Account sidebar with Login/Register, Recent, and the "Create new project" and
"3mf" tiles. Not a wizard, not a blank window. The GUI port works.

Two bugs stand between that and a usable app, both diagnosed from the crash
reports (`.ips`) off the device:

### 1. Any touch kills the process (fixed, unverified)

    NSInvalidArgumentException: unrecognized selector sent to instance 0x...
      -[UIResponder doesNotRecognizeSelector:]
      -[UIGestureRecognizerTarget _sendActionWithGestureRecognizer:]
      _UIGestureRecognizerSendTargetActions
      -[UIGestureEnvironment _updateForEvent:window:]
      -[UIWindow sendEvent:]

Three of the four crash reports are this, and the app had been alive for 46 s
before the first one — it dies on interaction, not on startup.

Patch 0209 attaches a `UIPanGestureRecognizer` and a `UIHoverGestureRecognizer`
to **every** `UIView` wx wraps, with the view itself as target and
`WX_scrollGesture:` / `WX_hoverGesture:` as actions. Those two methods are
installed only by `wxOSXIPhoneClassAddWXMethods()`, i.e. only on the classes wx
builds for its own controls. The GL canvas, a `WKWebView`, any plain `UIView`
container — none of them implement the selector, and the first scroll or
pointer move over one aborts the process.

Fixed by `class_addMethod`-ing the two IMPs onto the view's own class when
`respondsToSelector:` says they are missing, rather than skipping the
recognizers — the 3D canvas is precisely where scroll-to-zoom has to work. The
names are `WX_`-prefixed so adding them to a UIKit class collides with nothing.

### 2. wx was built with asserts live (fixed, unverified)

A modal **wxWidgets Debug Alert** appears over the UI:

    src/common/sizer.cpp(2324): assert "CheckSizerFlags(!((flags) &
    (wxALIGN_RIGHT)))" failed in DoInsert(): wxALIGN_RIGHT will be ignored in
    this sizer: only vertical alignment flags can be used in horizontal sizers

No wx cmake invocation in this repo ever passed `CMAKE_BUILD_TYPE`, so wx built
in the empty config: no `NDEBUG`, so `wxDEBUG_LEVEL` stayed 1 and every
`wxASSERT` in wx's own sources shipped. Orca is built `Release`. Two
consequences, and the second is the nastier one:

- the alert, and probably the fourth crash report too — the one where an
  exception escapes `GUI_App::OnInit()` through `generic_exception_handle()` and
  `__cxa_rethrow`s into `terminate` less than a second after launch. An assert
  firing before any window exists cannot show a dialog.
- **a debug/release mismatch between wx and the app that links it.**
  `wxDEBUG_LEVEL` changes struct layout in parts of wx; the library and Orca
  disagreed about it in every build so far.

Fixed by adding `-DCMAKE_BUILD_TYPE=Release` to all six wx cmake invocations
(step2, step3-gui, step4, wx-probe, wx-only, device-ipa). The wx cache keys do
**not** hash the cmake flags, only `wx-overlay` + `patches/step2`, so the keys
were bumped (`ios-wxprefix-v3` -> `v4`, `ios-wxprefix-device-v1` -> `v2`) to
stop a cached assert-enabled prefix from being served to the fix.

## The simulator harness is lying again (open)

Step-3 run 64 linked clean (0 failed targets, 0 undefined symbols) and then
reported `first seen alive: never`, `crash reports: 0`, with **empty**
`sim-install.log`, `sim-launch.log` and `orca-app-log.txt`, and a 180x240
screenshot that is not the app. The device runs the same commit fine, so this
is the harness, not the app. Do not read a step-3 launch verdict as evidence
about the app until `sim-install.log` is non-empty. The device is the ground
truth now; the simulator is the thing that needs fixing.

## ccache is missing on step 4 (~~open, costs ~65 min a run~~ CLOSED)

> **CLOSED (2026-08-10).** Fixed by turning the PCH off (`-DSLIC3R_PCH=OFF`) —
> see "Build speed: root cause found" above, and the measurement in the
> 2026-08-10 section at the end of this file. Runs 96 and 99 both report
> **635 `direct_cache_hit` against 1-3 misses**, and run 106's
> `[7/9] Build + link` took **0.1 min**. A step-4 run that still compiles for an
> hour has an empty cache, not a broken one — check whether
> `Restore ccache` took 0.0 min, which means no entry existed in that branch's
> cache scope at all.

Run 26 restored `ccache-step4-` in 5 s and then spent 64 min compiling, same as
the cold run 25. Every step-4 iteration therefore costs ~75 min end to end even
with deps and wx cached. Worth one run with `ccache -s` published to find out
whether the entry is empty or the hashes miss.

## Why secondary windows do not appear (analysed, NOT yet fixed)

The user's report — "every separate window should open as a dialog, printer
setup for example" — has a concrete cause in `src/osx/iphone/nonownedwnd.mm`
(read at the pinned wx ref, v3.3.2):

    void wxNonOwnedWindowIPhoneImpl::Create(...)
    {
        m_macWindow = [UIWindow alloc];
        UIWindowLevel level = UIWindowLevelNormal;
        ...
        else if ( ( style & wxCAPTION ) ) { }      // <- dialogs land here
        ...
        CGRect r = CGRectMake( 0, 0, size.x, size.y );
        [m_macWindow initWithFrame:r];
        [m_macWindow setHidden:YES];
        [m_macWindow setWindowLevel:level];
    }

    bool wxNonOwnedWindowIPhoneImpl::Show(bool show)
    {
        [m_macWindow setHidden:(show ? NO : YES)];
        ...
        [m_macWindow makeKeyWindow];
    }

Every wxTopLevelWindow becomes **its own UIWindow**. A wxDialog carries
`wxCAPTION`, so it keeps `UIWindowLevelNormal` — the same level as the main
frame — and UIKit gives no defined z-order between windows at equal level.
`makeKeyWindow` changes key status, not order, and is not `makeKeyAndVisible`.
So a dialog raised over the main frame can simply never be seen. If it were
seen it would still be wrong: `CGRectMake(0, 0, size.x, size.y)` puts it at the
top-left corner at its desktop size, with no centring and no dimmed backdrop.

The fix is a rework of this file — level above the main frame for dialogs,
`makeKeyAndVisible`, centre or full-screen geometry — and possibly presenting
secondary TLWs as view controllers over the root window instead of as separate
UIWindows, which is what iOS actually wants. **Deliberately not attempted
blind.** The main frame goes through this same code path and currently works;
a wrong guess here costs a ~75 minute build plus a sideload and could leave the
app showing nothing at all. Do it once the on-device log exists (patch 0337),
so the change can be checked rather than hoped at.

Note also: the app has no `UIApplicationSceneManifest`, so it runs in the
legacy non-scene UIWindow mode. That is why a bare `[[UIWindow alloc]
initWithFrame:]` displays at all — under a scene lifecycle it would need a
`windowScene` and would show nothing. Do not add a scene manifest without
rewriting this file first.

# SESSION 2026-08-06: the simulator loop, and where the viewport stands

Read this section first. It supersedes the CI-infrastructure notes above.

## The loop that removes the human from the cycle

Two workflows now, and the second one is the point:

- `ios-step3-gui.yml` builds the simulator app (~75 min) and now **uploads the
  bundle as the `orca-sim-app` artifact**.
- `ios-sim-drive.yml` takes that artifact and does nothing but run the app:
  boot a fresh iPad simulator, install, launch, screenshot, collect the app's
  log, the startup-error note and any crash report, and publish the lot to
  `ci-logs/simdrive-run-N`. **Ten minutes, no rebuild, no sideload.**

It triggers on pushes to its own two files (`ios-sim-drive.yml`,
`tools/ci/sim-drive.sh`). That is not laziness: a `workflow_dispatch`-only
workflow that has never existed on the default branch **404s** when dispatched
through the API on a feature branch.

**Reaching a page needs no touch synthesis.** `GUI_App::on_init_inner` passes
`init_params->input_files` to `plater()->load_files()`, so a positional
argument loads a model and brings the 3D editor up. `simctl` forwards trailing
arguments, and a simulator app's paths are host paths, so the model is copied
into the app container first. Three rounds were wasted on `idb` before this:
`fb-idb` breaks on Python 3.14 (`asyncio.get_event_loop()` raises), then wants
`/usr/local/bin/idb_companion` (Intel prefix, these runners are arm64), and
`idb_companion` does not install at all any more - the `facebook/fb` tap is
defunct. **Do not spend more time on idb.**

## Harness lessons paid for in full

- **A backgrounded `--console-pty` launch produces no pid line and no output
  when the app dies early** - identical to simctl refusing to launch. Runs 64
  and 66 were both read as "the app never started" on that evidence; run 66 was
  perfectly healthy. Never conclude "never alive" from an empty console log.
- **Do not launch twice.** A plain launch followed by a `--console-pty` launch
  with `--terminate-running-process` kills the first instance; drive run 5's
  "died during the walk" was that, not the app.
- **Simulator crash reports live in four places**, not one - the host's
  DiagnosticReports, `~/Library/Logs/CoreSimulator/<udid>`, and two directories
  inside the device's own data root.
- **`log show --predicate 'process == "OrcaSlicer"'` matches nothing** when the
  app dies before the log subsystem attributes anything to it. Ask for the
  bundle id and executable name from any process, and capture SpringBoard,
  launchd and runningboardd separately.

## Fixed and verified in the simulator

- **The wxWidgets Debug Alert is gone.** `CMAKE_BUILD_TYPE=Release` does *not*
  disable wx's asserts - run 65's screenshot has the `sizer.cpp(2324)` alert
  sitting in the middle of it. `-DwxBUILD_DEBUG_LEVEL=0` does; wx turns it into
  `-DwxDEBUG_LEVEL=0` for the whole library (`build/cmake/init.cmake`). The wx
  cache keys hash `wx-overlay` and `patches/step2` but never the cmake flags,
  so they must be bumped by hand whenever a flag changes.
- **The app starts and stays up** for the full observation window, renders the
  home page, loads a model, and reaches the Prepare page with its entire
  sidebar drawn correctly.
- **The instant startup crash is gone**, which points at the hardened 0337 -
  logging setup must never be able to throw out of `on_init_inner`.

## NOT fixed: the 3D viewport is still empty

This is the open bug. Prepare renders everything except the canvas, and the
console still carries:

    Failed to bind EAGLDrawable: <CAEAGLLayer: 0x...> to GL_RENDERBUFFER 1

`wxGLContext::SetCurrent` used to call `-bindDrawable` only inside
`if (v.context != m_glContext)`, so a bind that failed while the layer had no
size was never retried. Patch 0214 retries while the drawable reports no size
and binds after making the context current. **It was not enough** - the retry
fires and the bind still fails, which means the layer is persistently unable to
back a renderbuffer rather than merely not ready.

0214 now also reports, once, what the view looks like at the moment of failure:
bounds, frame, contentScaleFactor, whether it has a window and a superview,
whether it is hidden, and the layer class and bounds. That single NSLog line
separates "called too early" from "wrong geometry" from "not in the hierarchy",
and it is the next thing to read.

Ruled out already, do not re-investigate: the ES shader set (38 files, all
`#version 300 es`, all with precision qualifiers, no legacy constructs), the
shader list the ES path requests (matches what ships), and the GLKit
default-framebuffer wrapper in patch 0329 (correct).

---

# SESSION 2026-08-06/07: dialogs, the webview, and the bug that made the
# wizard a no-op

Read this section first. It supersedes everything above it where they disagree.

## Headline: `wxDialog::ShowModal()` never returned on iOS

This is the bug behind almost every symptom the user reported in the last two
rounds — the empty sidebar, the blank printer combo, the empty filament box,
the missing build plate, the setup wizard reappearing on every launch, and the
crash on the second launch. One bug, six symptoms.

`patches/step2/0218-cf-loop-bounded-exit-drain.patch` fixes it.

### The mechanism

`wxCFEventLoop::OSXDoRun()` (`src/osx/core/evtloop_cf.cpp`) drains queued
events before it returns:

```cpp
if ( m_shouldExit )
{
    while ( DoProcessEvents() == 1 )
        ;
    break;
}
```

`DoProcessEvents()` → `DispatchTimeout(1000)` → `DoDispatchTimeout()` →
`CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, /*returnAfterSourceHandled*/ true)`.
That returns `kCFRunLoopRunHandledSource` — mapped to **1** — as soon as it
services *any* source. A live UIKit app always has another one ready: the
display link driving the GL canvas, WebKit's IPC ports, a `wxTimer`. So the
condition never went false and the drain was an infinite loop.

macOS never hits this. There `wxGUIEventLoop::OSXDoRun` is `[NSApp run]` and
`wxModalEventLoop::OSXDoRun` is `[NSApp runModalForWindow:]`; neither touches
this function. On iPhone the modal loop *does* — because patch 0216 (previous
session) replaced the empty `wxModalEventLoop::OSXDoRun` stub with
`wxCFEventLoop::OSXDoRun()`. 0216 was right and necessary; it just landed us on
this landmine.

The fix bounds the drain (`for (int i = 0; i < 32 && DoProcessEvents() == 1; ++i)`).
Anything still queued is handled by the loop we return into.

### How the log proved it (this is the technique to reuse)

Patch 0216 logs `modal loop ENTER` / `STOP requested` / `EXIT rc=`. The device
log from 2026-08-07 08:55:57 has **three ENTERs, three STOPs and one EXIT**:

```
08:55:58 modal loop ENTER   (Setup Wizard)
08:56:21 modal loop STOP requested        <- no EXIT
08:56:30 modal loop ENTER   (wizard again, opened from the printer combo)
08:56:42 modal loop STOP requested        <- no EXIT
08:56:48 modal loop ENTER   ("Orca Slicer info")
08:57:00 modal loop STOP requested
08:57:07 modal loop EXIT rc=5100          <- 46s after the first STOP
```

`OSXDoRun`'s outer loop checks `m_shouldExit` at least once a second (the
`DoProcessEvents()` timeout is 1000 ms), so a stop is *always* noticed within a
second. Therefore the hang can only be in the drain. That is a proof, not a
guess.

### Why it produced exactly those symptoms

`GuideFrame::run()` (`WebGuideDialog.cpp`) is:

```cpp
int result = this->ShowModal();
if (result == wxID_OK) {
    this->apply_config(app.app_config, app.preset_bundle, app.preset_updater, ...);
    app.update_mode();
    ...
}
```

The wizard's own `user_guide_finish` handler calls `SaveProfile()`, which
writes `firstguide/finish = "1"` and `app_config->save()` — that part *did*
run. But the vendor / printer-model / filament selections live in
`m_appconfig_new` and are only pushed into the real `AppConfig` and
`PresetBundle` by **`apply_config()`, after `ShowModal()`**. That never ran. So:

- the printer combo had no selected preset → blank row, only the three
  "System presets / --Select/Remove printers-- / --Create printer--" entries;
- no filaments → empty grey box;
- no bed/plate selected → nothing to render in the viewport;
- `preset_bundle->printers.only_default_printers()` stayed true, so
  `GUI_App::config_wizard_startup()` re-ran the wizard on the next launch;
- three modal loops piled up on the C++ stack and the process died while
  unwinding them (the earlier `objc_release` SIGSEGV crash report).

**Independent confirmation of "firstguide was written but presets were not":**
`GUI_App::run_wizard` picks the dialog style from that flag —

```cpp
long pStyle = wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU;      // 0x20081800
if (strFinish == "false" || strFinish.empty())
    pStyle = wxCAPTION | wxTAB_TRAVERSAL;                    // 0x20080000
```

and patch 0215 logs the style on every `TLW SHOW`. Launch 1 showed
`style=0x20080000`, launches 2 and 3 showed `style=0x20081800`. The flag was
persisted; the presets were not. Decoding that style number is how you tell
"first run" from "re-run" without any extra instrumentation.

## Everything else fixed this session

| Patch | What it does |
|---|---|
| `step2/0215-iphone-secondary-windows-as-dialogs.patch` | Secondary `wxTopLevelWindow`s get their own `UIWindow`, centred, clamped to the screen, `windowLevel = UIWindowLevelAlert+1`, `makeKeyAndVisible`. Also fixes `Create()` ignoring `pos`. Logs every `TLW SHOW`/`HIDE` with class, title, **style**, popup/main flags, frame and level. |
| `step2/0216-iphone-real-modal-event-loop.patch` | `wxModalEventLoop::OSXDoRun`/`OSXDoStop` were empty stubs — every `ShowModal()` returned instantly, so the config wizard and every login/error dialog flashed and vanished. Now runs the CF loop. Logs ENTER/STOP/EXIT. |
| `step2/0217-iphone-webview-file-url-read-access.patch` | `loadFileRequest:allowingReadAccessToURL:` for `file:` URLs, `javaScriptCanOpenWindowsAutomatically = YES`, in-place load for `createWebViewWithConfiguration`, navigation ALLOW/CANCEL and LOAD FAILED logging, and **no synchronous `RunScript` in `AddScriptMessageHandler`** (kept `AddUserScript`). |
| `step2/0218-cf-loop-bounded-exit-drain.patch` | The headline fix above. |
| `step3/0340-ios-window-tree-dump.patch` | `orca-ios-ui:` window tree dump 20 s after `MainFrame` — class, label, id, position, size, best size, shown/HIDDEN/disabled/OFFSCREEN for every `wxWindow`. Plus an in-app driver gated on `ORCA_IOS_DRIVE`. |
| `step3/0341-no-cwd-in-guide-profile-load.patch` | `orca_absolute_no_cwd()` replaces seven one-arg `boost::filesystem::absolute()` calls in `WebGuideDialog.cpp`; drops a 25 MB JSON log line; clamps the iOS log level to `warning`. **Log went from 49 MB / 61 s to 320 KB.** |
| `step3/0342-ios-no-network-plugin-download.patch` | Guards `GUI_App::updating_bambu_networking()` and `ShowDownNetPluginDlg()` on iOS. Confirmed working — the "Install Bambu Network plugin" screen is gone from the 08:55 log. |
| `tools/ci/make-ios-icons.py` | Stdlib-only PNG decode/crop/flatten/downscale → `AppIcon.appiconset`; both workflows run it + `actool` and merge the partial plist. **User confirmed the icon works.** |

## Root causes found and fixed earlier in this session (all confirmed on device)

- **`boost::filesystem::absolute(p)` is `absolute(p, current_path())`** — the
  default argument is always evaluated, and `getcwd()` is EPERM on iOS outside
  the sandbox working directory. This made `LoadProfileData` throw. Any
  remaining one-arg `absolute()` / `current_path()` call is a live landmine;
  the step3 workflow now lists them at the end of the build.
- **A 25 MB `strAll` JSON dump at info level** was 60 s of the startup time.
- **`loadFileURL:` rejects URLs with a query string.** Orca's guide pages are
  `index.html?target=1&lang=en_US`, so switching to `loadFileURL:` blanked every
  webview. `loadFileRequest:allowingReadAccessToURL:` (iOS 15+) is the right API.
- **`stringByResolvingSymlinksInPath` strips a leading `/private`.** Both sides
  of a read-access prefix comparison must use the same normalisation, and the
  URL you load must be the normalised one.
- **`WKPreferences.javaScriptCanOpenWindowsAutomatically` defaults to NO on
  iOS** (YES on macOS). It silently blocked the wizard's `window.open` jump.
  `orca-overlay/resources/web/guide/0/load.js` also now uses
  `window.location.replace()` instead of `window.open(..., '_self')`.
- **`wxWebView::RunScript` is synchronous** — it waits in
  `wxEventLoopBase::YieldFor`, which pumps a nested CFRunLoop. Calling it from
  `AddScriptMessageHandler` inside a `CallAfter` inside `ShowModal` popped an
  autorelease pool underneath one in flight (the `objc_release` SIGSEGV).

## Diagnostics to use next time — do not re-derive these

1. **`wx-ios TLW SHOW/HIDE`** (0215): tells you whether a window was created,
   where, how big, and at what level. The `style=0x…` field distinguishes code
   paths (see the wizard example above).
2. **`wx-ios: modal loop ENTER/STOP/EXIT`** (0216): an ENTER with no matching
   EXIT means the code after `ShowModal()` never ran. **Always count these.**
3. **`orca-ios-ui:` window tree** (0340): separates "never created" from
   "created 0 px wide" from "created off screen" from "hidden".
4. **`wx-ios webview: loadFileRequest … (read access …)` / `navigation ALLOW` /
   `LOAD FAILED`** (0217): the whole webview story in three line types.
5. **`orca-ios-webview: evaluateJavaScript:` / `FAILED:`** (0333): every script
   Orca injects and every JS exception.

Reading order for a device log: `grep "modal loop"` first, then
`grep "wx-ios TLW"`, then the tree.

## Still open

- **`window.postMessage()` with no arguments** is being evaluated on the
  homepage webview and throws `A JavaScript exception occurred`. Present on
  every launch. Probably harmless, but it is the last line before the process
  died on the two crash-loop launches (08:58:48 and 08:58:52), so it is worth
  ten minutes. Find the `RunScript` caller that formats an empty payload.
- **The log level clamp costs breadcrumbs.** 0341 forces iOS to `warning`, which
  hides `run wizard...`, `GuideFrame returned ok`, `finished run wizard` and the
  `OnScriptMessage` trace. Promote those specific lines to error rather than
  raising the global level — that is the cheapest next diagnostic win, and it
  would have shown the modal-loop bug in one line.
- **The window tree is dumped only once, 20 s after `MainFrame`**, which is
  while the wizard is still up and before any presets exist. Add dumps at ~90 s
  and ~180 s (the timer block is at the end of `MainFrame::MainFrame()` in
  patch 0340) so there is a tree of the Prepare page in its loaded state.
- **The app reports the screen as 1600×1200** while an M4 iPad Pro is
  1032×1376 points. The GL canvas reports `2070x1959`. Something is running
  scaled; this may explain broad UI misalignment. Not investigated.
- **ccache is at 88 % miss on step 3/4** — a constant 639 total / 76 hit / 563
  miss across runs. Not explained. This is most of the ~60-70 minute build time.
- **The 3D viewport** (previous session's open bug) — the EAGL drawable bind.
  Note the plater has never yet had a valid printer preset, so "nothing renders"
  has had two possible causes all along. Re-test after this build.

## Environment facts that changed conclusions

- The user's device is an **iPad Pro M4 (2024) on iOS 27 beta 4**. The runner's
  simulator is **iOS 26.2** — a full major version apart. "It worked in the
  simulator" is therefore *not* evidence that it works on the device. A
  hypothesis was wrongly discarded once on that basis; do not repeat it.
- The user runs **Bambu A1 in LAN-only mode**, has already made LAN work on the
  `lan-backend` branch, and does not want the Bambu network plugin prompt at
  all. Branch audit done this session: the working branch's 68 patches are a
  strict superset of `main` (59) and `lan-backend` (58); no non-patch files are
  missing; only 4 patches differ in content, all changed this session, none
  LAN-related. **No LAN progress has been lost.**
- **`idb` does not work on the runner.** `brew install idb-companion` fails and
  every `idb` call dies on a missing `/usr/local/bin/idb_companion`. That is why
  the in-app driver (`ORCA_IOS_DRIVE`, patch 0340) exists — the app presses its
  own buttons.

## CI facts

- **`ios-step4-device-ipa.yml` is `workflow_dispatch` only.** It used to trigger
  on pushes matching `paths: patches/**`, and because it has
  `cancel-in-progress`, an unrelated commit killed a running device build. Do
  not put a push trigger back on it.
- `ios-step3-gui.yml` has `concurrency: group: step3-gui-${{ github.ref }},
  cancel-in-progress: true`.
- **`WX_KEY` hashes `wx-overlay/**` + `patches/step2/*.patch`.** Any step2 patch
  invalidates the wxWidgets prefix cache and adds a full wx rebuild to the run.
  0218 is a step2 patch, so build 66 pays that cost.
- Builds take **60-75 minutes**. Do not promise less. An earlier estimate of
  ~37 minutes based on a wx cache hit was wrong: the wx build was skipped and
  the Orca compile still took 36+ minutes on its own.
- GitHub had an **Actions incident from 15:22 UTC on 2026-08-06**; queue delays
  and cancel 502s that day were that, not runner congestion.

## Where things stand right now

- Branch: `claude/ipad-build-failure-dkutxc`, HEAD `f6a6c7b`
  ("Make modal dialogs actually return on iOS").
- **Step 4 device-IPA run 66 is queued on `f6a6c7b`.** Run 65 (on `5925087`,
  without the modal fix) was cancelled to free the runner.
- What to check on the device once run 66's IPA is installed, in order:
  1. Complete the setup wizard, pick the A1 and a filament.
  2. `grep "modal loop"` in the log — every ENTER must have an EXIT within a
     second of its STOP.
  3. The sidebar should show the printer and filament; the plate should render.
  4. Relaunch. The wizard must **not** come back, and the app must not crash.


## Loose ends from this session that belong in the record

### `wxIPhoneToggleButtonPeer` (in `wx-overlay/src/osx/iphone/extra_peers.mm`)

Orca's `CheckBox` and `SwitchButton` widgets are both `wxBitmapToggleButton`.
On the iPhone port `wxWidgetIPhoneImpl::SetBitmap`, `SetValue` and `GetValue`
are **empty bodies**, and the peer it created was a bare `UIView` — which is
not a `UIControl`, so it received no touches at all. Every checkbox and switch
in the app was invisible, unclickable and permanently `false`.

`extra_peers.mm` now defines `wxIPhoneToggleButtonPeer`, backed by a real
`UIButton`, implementing `GetValue`/`SetValue`/`SetBitmap`/`GetBitmap` and a
`controlAction` that flips the value on `TouchUpInside` **before**
`OSXHandleClicked` reads it. This is overlay code, not a patch — the overlay
files are copied over the wx tree after the step2 patches are applied, and they
are part of `WX_KEY`, so touching them also forces a wx rebuild.

### How to verify patches before burning an hour of CI

The patches are **cumulative**, applied in filename order with plain
`git apply` from the source root. Consequences:

- `git apply --check` on individual patches is meaningless and will report
  false failures. Verify by applying the whole series **sequentially** against
  a fresh checkout.
- Index hashes in the diff headers are irrelevant to `git apply`; do not chase
  them.
- A patch generated against pristine upstream will conflict if an earlier patch
  edits the same region. This bit patch 0217 (generated against pristine wx,
  conflicted with 0210, had to be regenerated against the post-0210 file).
  Always generate a new patch from a checkout that already has its predecessors
  applied.

Recipe used this session to read upstream source without a full clone:

```
git clone --filter=blob:none --no-checkout <url> src && cd src
git sparse-checkout init --cone
git sparse-checkout set src/osx/core src/osx/iphone include/wx src/common
git checkout <ref>
```

wx: `https://github.com/SoftFever/Orca-deps-wxWidgets` @ `v3.3.2`
Orca: `https://github.com/SoftFever/OrcaSlicer` @ `395e070a0e`
(`src/slic3r/GUI` + `resources/web` is enough for the GUI questions).

Note the sparse checkout is fine for *reading* and for generating a diff of one
file, but `git apply` of the full series needs the files it touches to be
present.

### Where the device logs come from

The user exports the app's own log from the iPad — patch 0337 writes it to the
app's `Documents/` as `debug_<Day>_<Mon>_<DD>_<HH>_<MM>_<SS>_<pid>.log.0`, so
one file per launch and the pid is in the name. A crash loop shows up as
several short files a few seconds apart. `patches/step3/0338` also drops
`orca-startup-error.txt` next to it when startup throws.

Log level on iOS is clamped to `warning` by patch 0341, so anything you want to
see from the device must be logged at `warning` or `error`. That is why the
instrumentation in 0215/0216/0340 uses `BOOST_LOG_TRIVIAL(error)` and
`wxLogMessage` (wx messages arrive as `warning`).

### Workflows, and which one to use

| Workflow | Use it for | Trigger | Cost |
|---|---|---|---|
| `ios-step4-device-ipa.yml` | the real IPA for the iPad | dispatch only | 60-75 min |
| `ios-step3-gui.yml` | simulator `.app` + one launch | dispatch | ~75 min |
| `ios-sim-drive.yml` | **the fast loop** — takes an existing `orca-sim-app` artifact and walks the UI, no compile | push to its own two files, or dispatch with `app_run_id` | minutes |
| `ios-step1-core-cli.yml`, `ios-step2-wxwidgets.yml` | deps / wx in isolation | dispatch | — |

`ios-sim-drive.yml` collects `orca-window-tree.txt`, `orca-ui-log.txt`, the
full gzipped log and screenshots into `ci-logs/simdrive-run-N/`. Use it for
anything that does not need a recompile. Remember the simulator is iOS 26.2 and
the device is iOS 27 beta 4.


### Branch state (2026-08-07)

`main` was fast-forwarded to the working branch at `d05018d`. **`main` and
`claude/ipad-build-failure-dkutxc` are now identical** — start from `main`.

Other branches on the remote, kept for reference only, all strictly behind:

- `claude/lan-backend-implementation-gxu2jr` — where the Bambu A1 LAN backend
  was built and tested. Its 58 patches are a subset of what is now on `main`;
  the LAN work itself lives in `orca-overlay/src/slic3r/Utils/BambuLan*` and
  `patches/step3/0335-ios-bambu-lan-agent.patch`, both present on `main`.
- `claude/step-4-ipa-parity-bkdym6`, `claude/basic-ipad-app-plan-pb0b4p`,
  `claude/merge-branches-to-main-78ua1g` — older tracks, superseded.

Audited this session: `main` now carries 8 step1 + 18 step2 + 43 step3 patches
plus both overlays. Nothing from any other branch is missing.


# SESSION 2026-08-07: the second-launch crash — one boost::filesystem call

## Headline

The app died on the second launch, and on every launch after it, in
`Sidebar::update_printer_thumbnail()` (`src/slic3r/GUI/Plater.cpp`), on

```
boost::filesystem::absolute(boost::filesystem::path(resources_dir()) /
                            "/profiles/" / vendor.id / cover_file)
```

`absolute(p)` is `absolute(p, current_path())` and evaluates that default
argument even when `p` is already absolute. On iOS `current_path()` throws

```
boost::filesystem::current_path: Operation not permitted [system:1]
```

Fixed by `patches/step3/0343-no-cwd-in-printer-thumbnail.patch` — the same
one-line helper as 0339 (Preset.cpp) and 0341 (WebGuideDialog.cpp) — plus
0344, which stops the process taking a working directory it cannot getcwd()
from in the first place, and 0345, which stops the JS exception on the
homepage.

## How the logs said it, and why the timing is the proof

Three device logs from 2026-08-07 (one launch each; the pid is in the name):

| Launch | Log | Ends |
|---|---|---|
| 1 | `10_28_10_5505` | two `Uncaught exception: …current_path…`, 10:28:34 and 10:28:39 |
| 2 | `10_28_40_5519` | 8526 bytes, no exception logged |
| 3 | `10_28_44_5528` | 8526 bytes, byte-identical shape to launch 2 |

`update_printer_thumbnail()` only reaches the `absolute()` call when a printer
model is selected **and** the matching vendor profile is installed —
`preset_bundle->vendors` is empty before the setup wizard runs. So:

- Launch 1 got all the way to the wizard fine, and threw 1.4 s after the wizard
  returned `rc=5100` (`modal loop EXIT` 10:28:32.765 → `calc_exclude_triangles`
  10:28:34.116 → throw 10:28:34.120). The static `printer_thumbnails` cache is
  never populated, because the throw happens before the line that populates it,
  so the second sidebar refresh threw again at 10:28:39.
- Launches 2 and 3 had a printer preset from the start, hit the same call while
  the main frame was still coming up, and died there.

**Why launch 2 logged nothing.** On launch 1 the throw unwound into
`GUI_App::OnInit`'s `try` (the wizard runs inside `on_init_inner`), which is why
it produced a log line *and* two lines in `Documents/orca-startup-error.txt` —
that file had exactly two lines, i.e. nothing from launches 2 and 3. Without a
wizard the same code runs from an event dispatched by UIKit, with no C++ frame
above it to catch anything, so the process goes away silently. **A silent death
on iOS does not mean "not an exception" — it means the throw did not unwind
through a C++ `try`.** Do not use "nothing in the log" as evidence again.

## Patches added

- **0343-no-cwd-in-printer-thumbnail** — `orca_absolute_no_cwd()` in Plater.cpp,
  and the filesystem calls moved *inside* the `try` that was already wrapping
  the bitmap load, so a thumbnail can never again cost the process.
- **0344-ios-keep-a-usable-working-directory** — `init_app_config()` chdir()s to
  `data_dir()/log` (GUI_App.cpp:2429 upstream). That directory is only useful on
  the desktop, where relative paths land next to the log; on iOS the log path is
  absolute (0337). On iOS the chdir now happens only if `getcwd()` still answers
  from the new directory, and reverts to the previous cwd (or `/`) if it does
  not. Either way it writes one line to the log at **error** level:
  `orca-ios-cwd: …`. **Read that line in the next device log** — it is the first
  direct measurement of where `getcwd()` works in this sandbox, and it settles a
  question three patches have now worked around by inference.
- **0345-no-empty-postmessage** — `GUI_App::get_login_info()` and
  `WebViewPanel::SendLoginInfo()` formatted `window.postMessage(%s)` with an
  empty command whenever the agent has no session (the LAN agent never does).
  `window.postMessage()` with no argument is a TypeError, which is the
  `orca-ios-webview: FAILED: A JavaScript exception occurred` seen on every
  launch. `post_logout_to_webview()` a few lines below already had the
  `!empty()` guard; this is the same guard where it was missing. Closes the
  first item on the previous session's "Still open" list — it was harmless,
  and it was not the crash.

## Verification done

All three apply cleanly with plain `git apply`, in filename order, to a fresh
checkout of `395e070a0e` with their predecessors applied (0321, 0335, 0336,
0338, 0342 are the only earlier patches touching `GUI_App.cpp`; nothing else
touches `Plater.cpp` or `WebViewDialog.cpp`). The `ios_chdir_to_log_dir()`
helper was compiled and run standalone with `-Wall -Wextra`. Not built for iOS
in this session — no CI run yet on these patches.

## Still open (carried forward, unchanged)

- The log level clamp costs breadcrumbs; promote the wizard/`OnScriptMessage`
  lines to error rather than raising the global level.
- Window-tree dumps only happen once, 20 s after `MainFrame`; add ~90 s/~180 s.
- The app reports the screen as 1600×1200 on a 1032×1376-point iPad Pro.
- ccache is at 88 % miss on step 3/4.
- The 3D viewport / EAGL drawable bind — and note the plater has never had a
  valid printer preset for long enough to render, because of the crash above.
  Re-test once this build is on the device.


# SESSION 2026-08-07 (later): confirmed fixed, and instrumentation for the next one

## Confirmed on device: the second-launch crash is gone

Run 68 (`851c679`, the 0343/0344/0345 build) installed on the iPad. Device log
`debug_Fri_Aug_07_11_59_10_5753.log.0`, a **fresh container**
(`3087EAD9-…`, so the old data dir was not reused):

```
[error] 11:59:10.320729 orca-ios-cwd: chdir to the log dir failed (errno 2); staying put, getcwd() works here
```

The wizard ran (`modal loop ENTER` 11:59:12.411 → `EXIT rc=5100` 11:59:26.546),
and **no `current_path` exception followed it** — that is the exact point where
every previous build threw. The sidebar came up with `Bambu Lab A1 0.4 nozzle`
and `Bambu PLA Basic @BBL A1`.

**What 0344 measured, and what is still unmeasured.** `getcwd()` works from the
working directory the app is launched on. This log is a first launch, so
`data_dir()/log` did not exist and the chdir failed with ENOENT — the revert
path never ran. Patch 0337 creates that directory, so the *second* launch on
this container will chdir into it successfully and exercise the revert. **Look
for `orca-ios-cwd:` in the next second-or-later launch log**: if it says
`getcwd() is refused inside the data dir`, that finally proves the sandbox is
what breaks `current_path()`, and 0339/0341/0343 were three symptoms of it.

## Open: the app dies on slice, and nothing says why

Same log, last four lines before it stops dead:

```
11:59:40.117  TLW SHOW: wxDialog "Loading..." 376x152
11:59:40.216  TLW HIDE: wxDialog "Loading..." 376x119
11:59:40.537  evaluateJavaScript: window.postMessage({"command":"orca_useroffline"})
              (the 2 s login timer never ticks again)
```

The `Loading...` dialog is the `ProgressDialog` in `Plater::priv::load_files`
(Plater.cpp:5989) — a model import, up for 99 ms. The process died within ~2 s
of it, with the user reporting they had pressed Slice.

Nothing more can be said from this log, and that is the actual problem:

- the log level is clamped to `warning` (0341), so `load_files`, `reslice`,
  `BackgroundSlicingProcess` and the G-code export log nothing at all;
- a SIGSEGV or a terminate leaves no line anywhere.

Candidates not yet distinguished, in rough order of suspicion: the first real
GLVolume upload on the GL ES path (the plater has never had geometry on it
before — this is the same unresolved area as the empty viewport); the toolpath
preview (libvgcode) build after slicing; TBB inside `Print::process`. Note
`BackgroundSlicingProcess::thread_proc` catches `std::exception` and reports it,
so a plain slicing exception should surface as an error dialog, not a death.

## Added: two patches whose whole job is to make the next report conclusive

- **0346-ios-crash-note-with-backtrace** — installs, from
  `set_log_path_and_level()` (the earliest well-defined point on iOS), handlers
  for SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT/SIGTRAP plus a `std::terminate`
  handler, writing **`Documents/orca-crash.txt`**: the signal or the
  exception's `what()`, the pid, the main image's load-address slide, and a
  `backtrace_symbols_fd` stack. Appends, so a crash loop is one file.
  - Signal-handler rules throughout: `write`/`open`/`backtrace_symbols_fd`
    only, no malloc, no stdio, no boost, no wx.
  - `sigaltstack` with a static 128 KB stack, so a stack overflow can still
    report itself. (A literal, not `SIGSTKSZ` — that is a `sysconf()` call on
    some libcs and this array needs static storage.)
  - The terminate handler is the important half: an exception thrown in a wx
    event handler on iOS unwinds into UIKit and never reaches
    `generic_exception_handle`. On the old build this would have printed
    `terminate: boost::filesystem::current_path: Operation not permitted` on
    launch two instead of nothing.
  - Both paths were compiled and **run** standalone (`-Wall -Wextra`): a null
    deref produced the report and still exited 139; a thrown
    `std::runtime_error` produced `terminate: <what()>` and still exited 134.
  - **To symbolicate:** `atos -o <the app binary from the run's artifact> -l
    <slide> <frame address>`. Keep the IPA of whatever build produced the trace.
- **0347-ios-log-level-override-file** — the clamp in `AppConfig::set_defaults`
  now yields to a level named in **`Documents/orca-log-level.txt`**
  (trace/debug/info/warning/error/fatal, case-insensitive). Drop the file in via
  the Files app, relaunch, get full logs; delete it to go back to `warning`. No
  rebuild, no 70-minute round trip. **Ask the user to set this to `info` before
  reproducing the slice crash.**

## What the window tree says about the left panel

From the 11:59:31 dump, matched against the user's screenshot. The panel is
built and populated — this is not a missing-widget problem, it is layout and
custom-control painting:

- **Preset combo text is not drawn.** `wxWindow "Bambu Lab A1"` (162x41, best
  250x41) and `wxWindow " PLA Basic"` (264x41) exist with the right labels and
  render blank. But a plain `ComboBox` in the settings area (`wxWindow
  "Aligned"`) *does* draw its text, and so do `0.4` and `Stainless Steel`. So it
  is `PlaterPresetComboBox` specifically, not Orca's `ComboBox` base.
- **The filament row is clipped.** `wxScrolledWindow id=-2226 … size=390x34`
  holds a 41 px tall combo. Same 30-vs-41 mismatch appears on several rows —
  containers sized in one metric, contents in another. Suspect the content
  scale / the app believing the screen is 1600x1200.
- **The process preset combo is absent from the tree entirely.** `wxPanel
  id=-2121` (the ParamsPanel, at y=220, 390x899, `best=0x0`) has exactly one
  child, the settings scroll at y=90. The 90 px band above it — where the Tab's
  header with the preset combo, save and delete buttons belongs — contains no
  windows at all. That matches the empty white block under "Process" in the
  screenshot.
- `wxWindow "Smooth High Temp Plate"` is 18 px wide (best 18x41): the bed-type
  combo is collapsed to nothing.

Not investigated further — it needs the app, not a log.


# SESSION 2026-08-07 (later still): why the Device page is blank, and pre-setup

## The Device page is not broken — there is no printer to show

`MonitorPanel::update_all()` (Monitor.cpp:341):

```cpp
obj = dev->get_selected_machine();
if (!obj) { show_status((int)MONITOR_NO_PRINTER); ... return; }
```

and `show_status(MONITOR_NO_PRINTER)` calls `set_default()`, which blanks the
page. The 11:59:31 window tree confirms the page itself is fully built and
populated — Status/Storage/Update/Assistant(HMS) rail, camera panel,
`MediaPlayCtrl`, printing-progress panel, Control panel with "Printer Parts".
Nothing is missing. `get_selected_machine()` simply returns null, so the user
sees an empty page and reads it as "doesn't load".

## Why no printer ever appears: iOS will not let this app do SSDP

Bambu LAN discovery is SSDP — the printer announces to multicast
`239.255.255.250` (`BambuLanDiscovery.cpp:42`, ports 2021 and 1990). Since
iOS 14, receiving multicast requires
`com.apple.developer.networking.multicast`, which Apple grants on request to a
paid developer account for a named App ID. **A sideloaded build cannot have
it.** `BambuLanDiscovery` already calls `IP_ADD_MEMBERSHIP` best-effort and
notes this in a comment; that best effort fails, and nothing is discovered.

The connection itself is *not* affected: it is a unicast TLS MQTT session to
the printer's own address (`BambuLanPrinterAgent::connect_printer`), which
needs no entitlement. Only the three facts SSDP would have carried are missing.

## 0348-ios-preconfigured-lan-printer

Answers the user's "can the printer be presetup" — and on iOS it is not a
convenience, it is the only route.

**The owner's own A1 is built in**, at their explicit request, so the app works
on their iPad with no setup step:

```
dev_id 03919D552413839   dev_ip 192.168.0.171   dev_type 3DPrinter-N2S-01
```

plus its LAN access code. The concern was raised first and reaffirmed: **this
repository is public, so that access code is public.** It is a LAN credential —
worth something only to someone already on the same network as the printer — and
it can be regenerated on the printer's own screen if it should stop being valid.
Do not extend this pattern to anything that is not a LAN credential.

Any field can be overridden per-device without a rebuild, which is the escape
hatch for a DHCP lease moving the printer:

```
<app>/Documents/orca-printer.json
{ "dev_id": "<serial>", "dev_ip": "192.168.0.171", "access_code": "<code>",
  "dev_name": "A1", "dev_type": "3DPrinter-N2S-01" }
```

Every key is optional and falls back to the built-in value, so overriding only
the IP is a one-line file; invalid JSON is logged and ignored rather than
leaving the app with no printer. It writes the access code to
`AppConfig["access_code"][dev_id]` — where `on_machine_alive` has
`MachineObject` read it from, DevManager.cpp:323 — then hands
`DeviceManager::on_machine_alive()` exactly the record SSDP would have
produced, and selects the machine once. From there it is Orca's ordinary LAN
path, untouched.

- Hooked into `GUI_App::post_init()` right after `m_agent->start_discovery()`.
- A 5 s beacon re-announces (Orca ages out machines that stop announcing) but
  **does not re-select**: `set_selected_machine()` on the same LAN dev_id
  disconnects and reconnects, so re-selecting on a timer would thrash the
  session.
- Logged at error level (`orca-ios-printer:`) so it survives the clamp.
- **The file is on the device, not in this repo, deliberately.** An access code
  is a credential for anyone on the network and this repository is public.
  Never commit one, and never bake one into a build.

Not yet built or tested on device.

## Two things to check when this lands

1. If the Device page fills in, discovery was the only thing missing and the
   whole LAN stack works. If it shows "connecting" and stays there, the MQTT
   path is next — turn the log level up with `orca-log-level.txt` (0347) and
   look for `BambuLanPrinterAgent`.
2. `dev_type` may need to be something other than `3DPrinter-N2S-01` —
   `_parse_printer_type()` maps it, and a wrong value gives the right printer
   with the wrong capabilities.


# SESSION 2026-08-07 (later still): the slice crash, from the OS crash report

## Root cause: glad never loaded OpenGL ES 3.0's core entry points

The user sent `OrcaSlicer20260807123035.ips` — the OS crash report, fully
symbolicated, which settled in one read what three log rounds could not.

```
exception: EXC_BAD_ACCESS, KERN_PROTECTION_FAILURE at 0x0
ktriageinfo: VM - Failed to fault in a page with execute permissions
esr: (Instruction Abort) Translation fault      pc = 0

Thread 0 (orcaslicer_main), triggered:
  0   ???                                        (imageIndex 2 = the null image)
  1   libvgcode::SegmentTemplate::render(unsigned long) + 124
  2   libvgcode::ViewerImpl::render_segments(...)
  3   libvgcode::ViewerImpl::render(...)
  4   Slic3r::GUI::GCodeViewer::render(int, int, int)
  5   Slic3r::GUI::GLCanvas3D::_render_gcode(int, int)
  6   Slic3r::GUI::GLCanvas3D::render(bool)
  7   Slic3r::GUI::GLCanvas3D::on_idle(wxIdleEvent&)
```

`pc = 0` with an *instruction* abort is a call through a null function pointer.
`SegmentTemplate::render()` makes three GL calls — `glGetIntegerv`,
`glBindVertexArray`, `glDrawArraysInstanced` — and the null one is the third:

- Orca links **one** glad, generated for **desktop** OpenGL. libvgcode declares
  against `<glad/gles2.h>` but takes its definitions from that shared target
  (patches 0323 and 0325).
- `gladLoadGL()` picks what to resolve from the version string:
  `GLAD_GL_VERSION_3_1 = (major == 3 && minor >= 1) || major > 3;`, and each
  `glad_gl_load_GL_VERSION_X_Y()` returns immediately if its flag is unset.
- An ES 3.0 context reports `OpenGL ES 3.0 …`, so glad reads **3.0** and loads
  nothing from desktop GL 3.1 upward.
- `glBindVertexArray` is desktop GL **3.0** → loaded, which is why
  `SegmentTemplate::init()` built its VAO and `render()` got past its
  `m_vao_id == 0` guard. `glDrawArraysInstanced` is desktop GL **3.1** → null.

**Forty-eight ES 3.0 core entry points were left null this way.** Computed, not
guessed: the functions declared in `src/libvgcode/glad/include/glad/gles2.h`,
intersected with those `src/glad/src/gl.c` only loads in a group above
`GL_VERSION_3_0`. Instanced drawing (3.1), attribute divisors (3.3), uniform
blocks (3.1), sync objects (3.2), immutable texture storage (4.2),
`glClearDepthf`/`glDepthRangef`/`glShaderBinary` (4.1), transform feedback
(4.0), samplers (3.3), `glInvalidateFramebuffer` (4.3).

## 0349-ios-es3-core-entry-points

Extends `orca_ios_install_gl_es_compat()` (patch 0328, which already runs
straight after `gladLoadGL` for exactly this class of problem) with a first
pass over a table of those 48 names, `dlsym`ing any slot still null. They are
all ES 3.0 core, so iOS exports them from OpenGLES.framework. The pass runs
*before* the existing no-op stubs so a real driver function is never shadowed
by one. It logs `orca-ios-gl: filled N …, M still null` at error level, and
names anything that would not resolve.

Compiled and run standalone with `-Wall -Wextra -Wstrict-aliasing=2`.

**Run 72 failed on this patch and the reason is a procedure lesson.** 0349 was
first generated against a tree with only 0328's `ios_gl_compat.cpp` applied —
but **0329 and 0333 also edit that file**, so the hunks did not match the real
series and `git apply` rejected them two minutes into the build. The handoff
already said to generate a patch from a checkout that has its predecessors
applied; a *filtered* subset is not that. Regenerated against the real
post-0348 file and re-verified.

### Verifying the series properly — the recipe that actually works

```bash
git clone --filter=blob:none --no-checkout <orca> full && cd full
git sparse-checkout init --no-cone
grep -h '^+++ b/\|^--- a/' patches/step*/*.patch | sed 's|^+++ b/||; s|^--- a/||' \
  | grep -v '^/dev/null' | sort -u > .git/info/sparse-checkout   # 47 files, no full checkout needed
git checkout <ORCA_REF>
for p in patches/step1/*.patch patches/step3/*.patch; do git apply "$p" || echo "FAILED $p"; done
```

To re-run it, reset with **`git reset --hard <ORCA_REF> && git clean -fd`**.
`git checkout <ref> -- .` is not enough: it does not remove the files earlier
patches *created* (`ios_platform_stubs.cpp`, `ios_gl_compat.cpp`,
`ios_webview_support.mm`), so the next pass fails with "already exists in
working directory" and sends you chasing a conflict that is not real.

Current state: **all 57 patches apply cleanly in order on a clean tree.**

## Run 73: `::sigemptyset` does not compile on Darwin

One compile error, in 0346:

```
utils.cpp:516:4: error: expected unqualified-id
        ::sigemptyset(&sa.sa_mask);
signal.h:125:26: note: expanded from macro 'sigemptyset'
  125 | #define sigemptyset(set)        (*(set) = 0, 0)
```

Darwin makes `sigemptyset` a **function-like macro**, so `::sigemptyset(x)`
expands to `::(*(&x) = 0, 0)`. glibc makes it a real function — which is exactly
why the standalone Linux test build of that handler passed. **A Linux
`-fsyntax-only` check does not prove Darwin will compile it**; the value of that
test is logic, not portability.

Audited the rest of the new code the same way afterwards: every other
`::`-qualified call in 0343-0349 resolves to a real function on Darwin
(`sigemptyset`, `sigfillset`, `sigaddset`, `sigdelset`, `sigismember` are the
function-like macros in `<signal.h>`, and only the first was used). `std::tolower`
is safe — libc++'s `<cctype>` undefines the C macro.

## What this probably also explains

The **empty 3D viewport**, open since 2026-08-06 and blamed on the EAGL
drawable bind. The plater's own render path may well be calling through other
members of the same 48. Re-test the viewport on this build before touching the
drawable code.

## Method note worth keeping

The OS `.ips` crash report is symbolicated, names every thread, and costs the
user two taps to export (Settings › Privacy & Security › Analytics & Improvements
› Analytics Data). **Ask for it first next time.** Patch 0346's own
`Documents/orca-crash.txt` is still worth having — it survives cases the OS
report misses, and it catches C++ exception text the OS never sees — but it is
the second-best source, not the first.


## The ccache report, read properly (run 73)

```
1124 Result: local_storage_write
 710 Result: local_storage_read_hit
 564 Result: local_storage_read_miss
 563 Result: direct_cache_miss
 563 Result: preprocessed_cache_miss
 562 Result: cache_miss
  75 Result: direct_cache_hit
```

What this rules out, and what it points at:

- **The cache is not cold and is not being ignored.** 710 `local_storage_read_hit`
  means ccache found and read that many entries — manifests included. The
  restore key works and the save works. "ccache never consulted" is dead as a
  theory.
- **`direct_cache_miss` 563 with manifests being found** is the signature of
  *the manifest exists for this command line, but one of the inputs it recorded
  hashes differently now*. Not a command-line change — that would show as
  manifests not being found at all.
- `preprocessed_cache_miss` equals `direct_cache_miss` exactly: the fallback
  preprocessor mode missed on all the same units, so the inputs genuinely differ
  rather than being a direct-mode-only artefact.
- Configuration is already correct for the usual suspects:
  `CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime,locale,system_headers`
  and `CCACHE_BASEDIR=$GITHUB_WORKSPACE`. So it is not header mtimes, not
  `__DATE__`, and not absolute paths in the command line.

**Leading hypothesis: the precompiled header.** Every libslic3r and
libslic3r_gui TU compiles with
`-Xclang -include-pch .../CMakeFiles/<target>.dir/cmake_pch.hxx.pch`, and ccache
hashes that `.pch` as one of the unit's inputs. The build directory is created
fresh every run, so the `.pch` is rebuilt every run, and a clang PCH serialises
the absolute paths and mtimes of everything it absorbed — of a tree that was
re-cloned minutes earlier. Different bytes, different hash, and every TU that
uses it misses. `pch_defines` sloppiness does not help with this: it governs how
the PCH may be *used*, not whether its content is hashed. The 75 hits would then
be the targets with no PCH.

That fits the constant 639 / 75 / 563 split the handoff has recorded across
runs, but it is **not yet proven**.

**Applied: `-DSLIC3R_PCH=0 -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`** in the step-4
configure. `SLIC3R_PCH` is Orca's own option (`CMakeLists.txt:124`, guarding
`add_precompiled_header` at `src/libslic3r/CMakeLists.txt:635`); that macro is a
wrapper around CMake's `target_precompile_headers`
(`cmake/modules/PrecompiledHeader.cmake:257`), which is what emits the
`cmake_pch.hxx` seen on the compile line. The CMake global is the backstop for
anything calling `target_precompile_headers` directly.

**Two things to expect.**

1. **The first run after this change is a full miss and will not be faster.**
   Dropping `-include-pch` changes every compile command line, so every manifest
   is new. The payoff is the run after that.
2. **It may not compile.** `FORCEINCLUDE` means every libslic3r TU currently gets
   `pchheader.hpp` whether it asks for it or not, so any source that has been
   relying on that for a declaration will now fail. `SLIC3R_PCH` is an
   upstream-supported option, so this should be rare, but it is the risk being
   taken. If it happens the answer is to put the PCH back, not to start adding
   includes — losing the cache is cheaper than diverging from upstream.

If the hit rate does not move on the second run, revert: a genuinely cold TU is
slower without a PCH, and the whole point was the cache.

**Why run 73's report could not settle it:** the report grepped `-B8` around
`Result: cache_miss`, and those eight lines are all stats-lock bookkeeping. The
reason lines sit much further up each unit's section. The report now ranks the
reason lines directly (`Include file … has changed`, manifest lines, anything
mentioning pch) and prints one whole unit's section with `-B60`. The next run
that produces a report will say which it is.


# SESSION 2026-08-07 (evening): slicing works; the UI does not

Run 74's build (`e2bad0f`) on the device. Four launches, 15:30-15:35.

## Confirmed working

```
orca-ios-gl: filled 48 ES 3.0 core entry points the desktop loader skipped, 0 still null
orca-ios-printer: selected Bambu Lab A1 (03919D552413839) at 192.168.0.171, access code set
```

**Slicing works.** 0349 was right and complete — every one of the 48 resolved.
The printer pre-setup registers and selects the machine.

## A measurement that contradicts an earlier theory — read this before trusting 0344

```
orca-ios-cwd: working directory is the log dir, getcwd() works     (3 of 4 launches)
orca-ios-cwd: chdir to the log dir failed (errno 2); staying put   (1, fresh container)
```

**getcwd() works fine from inside the app container.** The sandbox theory in
0344's comment is wrong, and 0344's revert path has never once fired. So what
actually fixed the `boost::filesystem::current_path: Operation not permitted`
crashes was 0339/0341/0343 removing the call sites — *not* 0344. Why
`current_path()` ever returned EPERM is still unexplained. 0344 is harmless and
its diagnostic is worth keeping, but do not cite it as the fix, and do not build
on its explanation.

## Found: the printer type was wrong, and it explains four separate symptoms

```
_parse_printer_type Unsupported printer type: 3DPrinter-N2S-01          (x12 a launch)
load_compatible_settings failed, file = .../printers/3DPrinter-N2S-01.json
```

`_parse_printer_type()` passes anything it does not recognise to
`DevPrinterConfigUtil::get_printer_type()`, which looks up
`resources/printers/<dev_type>.json`. The A1's file is **`N2S.json`**
(`"display_name": "Bambu Lab A1"`). The guessed `3DPrinter-N2S-01` matched
nothing, so the MachineObject had no recognised type and therefore no
capabilities — no bed model, no `ftp_folder` to upload to, no camera config.
That is one cause behind four reported symptoms: the Device page doing nothing,
sending to the printer failing, the build-plate sync failing, and the plate
being wrong. Fixed in 0348: `dev_type` defaults to `N2S`.

## Open, and the biggest one: the whole UI is laid out for the wrong screen

Reported: text stretched, checkboxes squished, buttons stretched, the nozzle
control drawn in front of another panel, the filament and printer names not
drawn at all, viewports intermittently wrong or frozen.

One number explains the class:

```
wx-ios TLW SHOW: wxFrame ... frame=0,0 1600x1200
```

The device is **iPad16,5** (13-inch iPad Pro M4): 2752x2064 px, documented as
1376x1032 points at 2x. It is being given **1600x1200 points**, and
2752/1600 = 2064/1200 = **exactly 1.72**. So the app draws into a non-native
logical canvas that the system rescales by a non-integer factor — which is
precisely what stretched text and squished controls look like.

Ruled out already: patch 0326 does set the frame from `wxDisplay`, and wx's iOS
implementation (`wxDisplayImplSingleiOS::GetGeometry` in
`src/osx/iphone/utils.mm`) is a straight `[[UIScreen mainScreen] bounds]`. So
wx is faithfully reporting what UIKit tells it. The Info.plist already carries
`UILaunchScreen`, `UIRequiresFullScreen`, `UIStatusBarHidden` and
`UIDeviceFamily=2`, so the usual causes of a scaled compatibility canvas are
not it either.

**0350 measures instead of guessing.** `orca_ios_log_display_metrics()` in
`ios_webview_support.mm` (already compiled, so no new CMake source and no wx
rebuild) logs, at error level, from `MainFrame`:

```
orca-ios-display (wx):        wxDisplay client area WxH, content scale S
orca-ios-display (MainFrame): UIScreen.bounds WxH, nativeBounds WxH,
                              scale S, nativeScale S, keyWindow WxH
```

`nativeBounds` vs `bounds` is the discriminator: if `nativeBounds` is
2752x2064 while `bounds` is 1600x1200 and `nativeScale` is 1.72, the system is
scaling the app and the fix is in the bundle, not the code. If `bounds` is the
documented 1376x1032 and something later resizes the frame to 1600x1200, the
fix is in wx or in Orca. **Read that line first next session.**

## Still open beyond that

- "Network plugin not detected" still shown on import, despite 0342.
- Viewport intermittently stuck after an import.
- Preset combo text (`PlaterPresetComboBox`) still not painted — may well be
  the same scaling problem rather than a separate paint bug; re-judge once the
  geometry is right.

## 0351: deeper logging, so a device round trip carries more than one fact

Four changes, all aimed at the same problem — the user can only report symptoms,
and each round trip costs an hour of CI plus a reinstall.

1. **OpenGL error checking is on again.** `3DScene.hpp` only defines
   `HAS_GLSAFE` when `!NDEBUG`, so on the iOS release build `glsafe(cmd)` was a
   bare `cmd` and every GL error was discarded — on the one platform where the
   GL path is new and undebuggable. That is part of why a null
   `glDrawArraysInstanced` took three rounds to find. Now defined on iOS too.
   **It costs nothing at the default level**: `glAssertRecentCallImpl()` returns
   immediately unless `get_logging_level() >= 5`, so it is one branch per GL
   call until someone asks for `trace`.
2. **The log level rises to `debug` 30 s after MainFrame**, when startup is
   over. The clamp in 0341 exists because a *first launch* at `info` writes
   49 MB in 61 s — nearly all of it the wizard's profile JSON, which is finished
   long before this fires. Everything after that point is the user importing,
   slicing and talking to the printer, which is exactly what has been invisible.
   Only ever raises, so `Documents/orca-log-level.txt` still wins.
3. **Every dialog is logged.** `MsgDialog`'s constructor is the base of
   `ErrorDialog`, `MessageDialog` and `WarningDialog`, so one line there catches
   all of them: `orca-ios-dialog: [title] headline`. `show_error()` logs at the
   call site too (`orca-ios-error:`), because it defers the dialog to an idle
   event and the call site is where the surrounding lines say what failed. This
   is what will finally name the "network plugin not detected" popup.
4. **Three window-tree dumps** (30 s, 60 s, 150 s) instead of one at 20 s, which
   on a first launch photographed the setup wizard and an empty plater.

To get the deepest level, put `trace` in `Documents/orca-log-level.txt`: that
turns on per-GL-call error reporting with file, line and function. Expect it to
be slow — it adds a driver round trip per GL call inside the render loop — so it
is for one reproduction, not for daily use.

## 0352: the two includes the PCH used to supply

Turning the PCH off (SLIC3R_PCH=0, for ccache) removed the force-included
`pchheader.hpp` from every libslic3r and libslic3r_gui TU, and two files were
relying on it:

```
LocalesUtils.cpp:79           std::stringstream buf;          -> <sstream>
PhysicalPrinterDialog.cpp:474 temp->GetToolTip()->Enable(..)  -> <wx/tooltip.h>
```

**Exactly two.** Run 77 built with `ninja -k 0`, so it compiled the whole tree
and reported every failure: `total FAILED: 2`. That is the authoritative list —
a grep for `std::stringstream` without `<sstream>` also flags `utils.cpp`, which
compiles fine because it picks the header up transitively. Do not trust the
grep; trust the `-k 0` run.

Run 77 also proves 0350 and 0351 compile: the display-metrics call, the dialog
logging, and `HAS_GLSAFE` being defined on iOS all went through the entire tree
without a diagnostic.

## Result: run 78 green, and the PCH hypothesis is confirmed

`54e8271` built successfully in **25 minutes**, against ~60 for every previous
device build. That is the ccache change paying off exactly as predicted: run 77
populated the cache with PCH-off objects, and run 78 hit them. The precompiled
header really was the cache-buster, and `SLIC3R_PCH=0` really is the fix.

Cost of getting there: run 76 (superseded), run 77 (the two missing includes).
Two builds to halve every build from here on.


# SESSION 2026-08-07 (night): the sidebar, diagnosed properly

A desktop screenshot of the same panel finally made a proper diff possible.
~25 defects, which sort into **three** causes, not twenty-five.

## Cause 1: owner-drawn text is not painted

Blank: printer name, filament name, the whole process preset row, every tab
label (Quality/Strength/...), and the icon buttons in the three header rows.
Painted correctly: the nozzle combo ("0.4"), the seam combo ("Aligned"), every
`wxStaticText`, every section header and icon.

Note the tab strip is **not missing** — its baseline rule and the teal
active-tab underline both draw. Only the labels are gone. Same for the preset
combos: `ComboBox::GetLabel()` returns the right string (the window tree shows
`wxWindow "Bambu Lab A1"`), and `TextInput::render()` draws it with
`dc.DrawText`. So the string is there and the paint runs; the text lands
somewhere invisible.

The suspect line is in `TextInput::render()`:

```cpp
pt.x += textSize.x;                                   // textSize = text_ctrl->GetSize()
pt.y = (size.y + textSize.y) / 2 - labelSize.y;       // <- grows negative as the font grows
```

The taller the label font relative to the control, the further **up** the text
is placed, until it is off the top. That single formula would produce both the
blank combos and the half-clipped "Nozzle" caption. Not yet proven.

## Cause 2: fonts and control heights are on different scales

`Label::sysFont()` has `#ifndef __APPLE__ size = size * 4 / 5; #endif` — iOS
takes the unscaled branch, because it is `__APPLE__`. Meanwhile a native iOS
`UITextField` is ~33 pt tall where a macOS one is ~21, which inflates every
`TextInput`/`ComboBox` to 41 pt (confirmed in the window tree) against ~28 on
the desktop. Captions render at body size and clip; row padding is inflated;
some spacers collapse to zero. Whether the right fix is the font branch, the
control metrics, or both is unresolved.

## Cause 3: the checkbox squash — found and fixed

`wxIPhoneToggleButtonPeer::SetBitmap` in `wx-overlay/src/osx/iphone/extra_peers.mm`
did `[b setImage:...]` and nothing else. A `UIButton` lays its image out inside
its content rect and `UIImageView` defaults to `UIViewContentModeScaleToFill`,
so an 18x18 tick is **stretched** to whatever that rect is — hence ~13x6 teal
slivers with no visible tick. Now sets `ScaleAspectFit`, zeroes the content and
image insets, and centres both alignments, so a wrong content rect can make the
artwork small but never squash it. It also `NSLog`s the button frame against the
image size (`orca-ios-toggle:`), which will say whether the frame is wrong too.

**This touches wx-overlay, so WX_KEY changes and wx rebuilds (~6 min).**

## The canvas: three IPAs from one compile

`UIScreen.bounds` 1600x1200 / `nativeBounds` 2064x2752 / `scale` 2 /
`nativeScale` 1.72 is iOS running the app on a compatibility canvas. wx 3.3.2's
iPhone port has no `UIScene` support and iPadOS puts non-scene apps in exactly
that mode. Since the Info.plist is written *after* the link, variants cost a
re-zip and nothing else — so the step-4 workflow now emits:

| artifact | difference |
|---|---|
| `OrcaSlicer-iPad.ipa` | baseline, unchanged |
| `OrcaSlicer-iPad-scene.ipa` | adds `UIApplicationSceneManifest` |
| `OrcaSlicer-iPad-windowed.ipa` | drops `UIRequiresFullScreen` |

Same bundle id in all three, so installing one over another keeps the data
directory and the printer config. Install one, read `orca-ios-display`, and the
question is answered without another build. **The scene variant may show a black
screen** — wx creates its `UIWindow` from the app delegate and has no scene
delegate. That is a real possible outcome, not a build failure.

## Still unfixed, deliberately

Causes 1 and 2 are not guessed at in this build. Both hypotheses are plausible
and both touch code that every widget in the app goes through; a wrong guess
there is a worse regression than the current state. The canvas result decides
which is even worth pursuing — if the app gets a native canvas, the font/metric
mismatch may resolve with it.

## 0353: the dead Device tab, and two instrumentation mistakes fixed

**The Device tab.** `Notebook::SetSelection()` relies on
`wxBookCtrlBase::DoSetSelection()` to show the page, and that only calls
`page->Show()` when the index actually changes. `MainFrame::show_device()`
inserts the Device page with `m_monitor->Show(false)`. If the insertion leaves
`m_selection` already pointing at that page, nothing ever shows it — and
`MainFrame::select_tab()` then skips the call entirely:

```cpp
if (m_tabpanel->GetSelection() != (int)new_selection)   // already equal
    m_tabpanel->SetSelection(new_selection);            // never runs
```

so the tap does nothing, permanently, while the page behind it is alive and
reporting `MONITOR_NORMAL`. The visible highlight never contradicts this because
`GetBtnsListCtrl()->SetSelection()` is a separate call.

Fixed on both sides: `Notebook::SetSelection` now shows the target page
unconditionally, and `select_tab` no longer skips when the target page is not
actually visible. It logs `orca-ios-tab:` with the requested index, the current
one, and whether the target was hidden — so if this is not the mechanism, the
next log says so immediately.

**Two instrumentation mistakes from the previous round, both mine:**

1. The toggle-geometry probe used `NSLog`, which writes to the device console —
   unreachable from a sideloaded app. It never reached the log file, so the
   round produced no measurement. Now `wxLogMessage`, which Orca routes into the
   boost sink and therefore into `Documents/log`.
2. Nothing recorded **which Info.plist variant was running**, which made the
   three-IPA canvas experiment unreadable: run 79's log shows 1600x1200 but
   there is no way to tell whether that was the baseline, `-scene` or
   `-windowed`. Now every launch logs
   `orca-ios-variant: sceneManifest yes/no, UIRequiresFullScreen yes/no`.

## Status after run 79 on the device

Confirmed changed: the settings tab labels (Quality/Strength/...) now draw, and
the checkboxes are square with a visible tick instead of ~13x6 slivers — the
`ScaleAspectFit` fix worked. They are still far too small (~6 pt against the
18 pt the control claims), which the toggle probe will now quantify.

Unchanged: `UIScreen.bounds` is still 1600x1200 with `nativeScale` 1.72. Whether
that means the variants do not help, or the baseline was the one installed, is
exactly what the variant line will settle.

## Run 80 on the device: three measurements, and what each settles

```
orca-ios-variant: sceneManifest no, UIRequiresFullScreen yes
orca-ios-display: UIScreen.bounds 1600x1200, nativeBounds 2064x2752, scale 2, nativeScale 1.72
orca-ios-toggle:  button frame 18x18, image 18x18 @2.0x        (x173)
orca-ios-tab:     select_tab 0, current -1, target hidden false
                  select_tab 1, current  0, target hidden true
                  select_tab 0, current  1, target hidden true
```

1. **The canvas experiment still has not run.** `sceneManifest no` means the
   *baseline* IPA was installed, twice now. The `-scene` and `-windowed`
   variants remain untested. Until one of them is installed, 1600x1200 tells us
   nothing new.
2. **The checkbox geometry is correct and the smallness is elsewhere.** The
   button is 18x18 pt and the image is 18x18 pt at @2x — both exactly right —
   and it still draws at roughly 7 pt. That rules out the frame and the bitmap,
   and leaves `UIButton`'s own content rect: on iOS 15+ that goes through
   `UIButtonConfiguration`, whose default insets dwarf an 18 pt control, so
   aspect-fit shrinks the image to fit what is left. 0353 therefore stops using
   `-setImage:forState:` altogether and gives the peer its own `UIImageView`
   pinned to `bounds`.
3. **The Device tab break is upstream of `select_tab`.** There is no
   `orca-ios-tab` line for the Device index at all — only 0 and 1, from
   startup. So the fix in 0353 (`Notebook::SetSelection` showing a hidden page)
   addressed a real defect but **not this one**: the tap never reaches
   `select_tab`. `target hidden true` on the 0/1 switches does show that defect
   was real and is now being corrected.

0354 instruments the missing link: every tab-button press logs
`orca-ios-tabbtn: pressed '<label>'` and the index it resolves to, and every
insertion into the strip logs its position and the resulting button count. A tap
that produces no line never reached the handler — a different bug from one that
resolves to the wrong page — and the insert lines will show whether the Device
button's index mapping is stale after `show_device()` rebuilds the strip.

## Run 81: the Device tab and the plugin popup are ONE bug, and it is found

The tab-button trace answered it immediately:

```
orca-ios-tabbtn: pressed ' Device', buttons 6     (x5)
orca-ios-tabbtn: resolves to index 3              (x5)
skipped tab switch from 2 to 3, lack of network plugins
```

The tap reaches the handler and resolves to the correct index. It is then
**vetoed**, in `Plater::priv::on_tab_selection_changing` (Plater.cpp:5130):

```cpp
if (preset_bundle->use_bbl_device_tab() && new_sel == MainFrame::tpMonitor) {
    if (!Slic3r::NetworkAgent::is_network_module_loaded()) {
        e.Veto();
        wxQueueEvent(q, new wxCommandEvent(EVT_INSTALL_PLUGIN_HINT));
    }
}
```

`is_network_module_loaded()` is `BBLNetworkPlugin::instance().is_loaded()` — a
dylib Bambu distributes at runtime, which a sideloaded iOS app can neither
download nor `dlopen`. Patch 0342 removed the download attempt; patch 0335
registered the native LAN agent under the same `bbl` slot instead. So the module
*is* present, it connects, and the log proves it — but this function said no.

**That single false answer produced both open symptoms**: the Device tab doing
nothing however healthy the connection, and the "network plugin not detected"
notice (`EVT_INSTALL_PLUGIN_HINT`, queued right next to the veto).

0355 makes `is_network_module_loaded()` return true on iOS. Note this is
load-bearing beyond the tab: other call sites gate features on it, so more of
the device UI may come alive at once.

## The canvas experiment: the default IPA is now the scene build

Three rounds running, `orca-ios-variant` reported `sceneManifest no` — the
baseline was installed every time and the experiment never ran. The workflow now
names the **scene** build `OrcaSlicer-iPad.ipa` and keeps the untouched one as
`OrcaSlicer-iPad-baseline.ipa`. Whatever is easiest to tap should be the arm
under test.

## Confirmed fixed on device

Checkboxes render correctly (`UIImageView` owned by the peer, 0353/this round).
`orca-ios-toggle` had already shown the frame and image were both 18x18 @2x, so
the fault was `UIButton`'s content rect, as suspected.

## Run 82 on device: the tab fix worked, printing works, and two questions closed

The user opened the Device tab for the first time, **sent a sliced model to the
printer and it started printing.** Then the app crashed.

**Closed: scenes are not the cause of the 1600x1200 canvas.**

```
orca-ios-variant: sceneManifest yes, UIRequiresFullScreen yes
orca-ios-display: UIScreen.bounds 1600x1200, nativeBounds 2064x2752, nativeScale 1.72
```

The scene build genuinely ran and the canvas did not move. `UIApplicationSceneManifest`
is ruled out. What has **not** been tested is dropping `UIRequiresFullScreen`,
which iPadOS 26+ deprecated and which is documented to place an app in a
compatibility mode — now the leading candidate, and now the default IPA
(scene + no UIRequiresFullScreen, built as the `both` variant).

**Closed: the Device tab veto is gone.** No `skipped tab switch` line anywhere.
0355 was correct.

## 0356: iOS kills the process for System V shared memory

```
exception: EXC_CRASH, signal SIGSYS, "Bad system call: 12"
  0  shmget + 8
  1  MediaPlayCtrl::get_stream_url(std::string*)
  2  MediaPlayCtrl::SetMachineObject(MachineObject*)
  3  MonitorPanel::update_all()
  4  MonitorPanel::Show(bool)
  5  wxBookCtrlBase::DoSetSelection(...)
```

`shmget` is not permitted in the iOS sandbox and does not fail politely — it
raises **SIGSYS**, which no handler can decline. Note the shape of this: it
became reachable *because* 0355 unblocked the tab. Showing the Device page
updates the monitor, which sets the machine object, which asks for the camera
stream URL over a shared-memory segment written by Bambu's camera tool — a
companion process that does not exist here. So the first successful Device tab
open killed the app immediately.

0356 returns false on iOS: there is no stream, which is the truth, and the rest
of the page is left alone.

**Worth a sweep:** `shmget`/`shmat`/`shmctl`/`ftok` are a class, not an
instance. This was the only one on a reachable path so far, but any other SysV
IPC in the tree will behave the same way — silent until reached, then SIGSYS.

## 0357: ask for local-network permission at launch

iOS gates every local-network connection behind a one-time prompt, and nothing
in the app raised it deliberately — so it appeared at whatever arbitrary moment
something first touched the LAN, or not at all, in which case the printer simply
never connects and the failure is silent.

`orca_ios_request_local_network_permission()` starts an `NSNetServiceBrowser`
for `_bambulab._tcp` (already declared in `NSBonjourServices`) from
`GUI_App::post_init`, immediately before the pre-configured printer is
announced. A Bonjour browse is the documented way to raise the prompt on demand;
it does not need to find anything, and the multicast entitlement is irrelevant
because raising the prompt is the whole purpose. The browser is held in a static
and stopped after six seconds — released immediately, it can cancel the request
before iOS has drawn the alert. Logs `orca-ios-lan:` when it fires.

---

# SESSION 2026-08-08/10: the viewport freeze, the camera, saving, printer
# errors, and the whole distribution chain

**Everything in the repo is on `main` as of this section.** The working branch
`claude/second-startup-crash-k0162l` was fast-forwarded into `main`; there is no
divergence to reconcile. Four older branches (`basic-ipad-app-plan-pb0b4p`,
`lan-backend-implementation-gxu2jr`, `merge-branches-to-main-78ua1g`,
`step-4-ipa-parity-bkdym6`) belong to a **different root commit** — the history
was restarted at `e2b55a7` — and are archaeology only. Checked: every path that
exists on those branches also exists on `main`, so nothing is stranded there.
Do not try to merge them; git reports no common ancestor.

## Where the port actually stands

**Confirmed working on the device** (the user drove each of these):

* Slicing, and **sending a sliced model to the printer over the LAN, which
  printed**.
* The sidebar: printer name, filament name, process name, all five preset tabs,
  the mode switch, correctly-sized checkboxes, distinct font sizes.
* Project save, and autosave into the app's Documents directory.
* The Device page, with the pre-configured A1 connected over the LAN.
* HMS printer error codes reaching the UI (`print_error 05004003` observed).
* **The printer's camera stream** (`BambuLanCamera: authenticated to
  192.168.0.171:6000`).

**Fixed but not yet confirmed on the device** — these are the first things to
ask the user about:

* The Prepare viewport freeze (wx patch 0223). See the next section; the
  diagnosis is solid and the fix follows from it, but nobody has dragged a model
  around on an iPad since it landed.
* The 1600x1200 canvas (SDK metadata in the Info.plist, run 104). Unproven, and
  frankly a guess — see "The canvas is still wrong".

**Last builds:** runs 100-106 of `ios-step4-device-ipa` are all green. Run 106
(`6928673`) is the newest IPA. Runs 96 and 99 failed to compile; both were
trivial and are described under "Compile failures worth not repeating".

## The viewport freeze: three wrong theories, then a measurement

This cost six device round trips. **Read this before touching rendering.**

Symptom, reported many times: a model loads, Prepare works for a few seconds,
then the 3D view stops responding to the finger — while continuing to draw.

**Wrong theory 1 — the render gate.** `_is_shown_on_screen()` was changed to
walk parents and skip the TLW. This produced a **launch crash** (run 94:
`Plater::priv` ctor → `update()` → `AssembleView::reload_scene()` → `render()` →
`init()` → `on_change_color_mode` → SIGSEGV); the gate had been the only thing
preventing rendering during Plater construction. A second attempt (a "latch",
run 95) gave a black Preview and a frozen Prepare. **Both were reverted;
`_is_shown_on_screen()` is upstream's `m_canvas->IsShownOnScreen()` and must
stay that way.**

**Wrong theory 2 — the framebuffer.** Kept anyway as 0374, because it is correct
on its own terms: on iOS the default framebuffer is not 0, and code that
restored 0 was unbinding GLKView's. It was not the freeze.

**Wrong theory 3 — rendering driven from idle.** Kept as 0364 (render from the
paint event). Also an improvement, also not the freeze.

**My own diagnostic was lying to me**, which is the part worth internalising.
The "no render in 5 s" report fired whenever the Plater was not the visible
page — sitting on the Device tab produced `wxPanel[HIDDEN]`, which I read as the
fault. 0373 made the report state its own verdict: `(plater selected: not
rendering is WRONG)` versus `(another page selected: not rendering is fine)`.
Every subsequent report said "fine". The render gate had never been stuck.

**The measurement that settled it** (0375 added a mouse-event counter alongside
the render counter, both printed every 5 s by `orca-ios-canvas:`):

```
render +123  mouse +125    <- working
render +1    mouse +0      <- "frozen", finger still moving on the glass
render +0    mouse +0
```

Zero mouse events. It was never a rendering bug — **the touches stopped
arriving.** Two compounding gaps in the wx iPhone port:

1. `touchesCancelled:withEvent:` was never registered on the view class, so
   UIKit's cancellation was delivered to nothing. Even where the phase was
   reachable, `UITouchPhaseCancelled` fell into `default:` and reported no
   event — leaving wx holding a captured mouse and Orca holding
   `m_mouse.dragging`.
2. `wxUIView` — what *every* wx pane is made of — is a **UIScrollView
   subclass**, so it ships with pan and pinch recognisers. A sustained drag
   across the canvas eventually reads as a scroll to an ancestor, and the moment
   it does, that ancestor cancels the touches it had been forwarding and keeps
   every one afterwards. Permanently.

`0223-iphone-cancelled-touches.patch` fixes both, in two files:

* `src/osx/iphone/window.mm` — a real `case UITouchPhaseCancelled:` that maps to
  LEFT/RIGHT/MIDDLE_UP by `gs_lastPointerButton` and clears it, plus the missing
  `class_addMethod(c, @selector(touchesCancelled:withEvent:), …)`.
* `src/osx/iphone/glcanvas.mm` — after `MacPostControlCreate`, walk the
  superview chain and set `canCancelContentTouches = NO; delaysContentTouches =
  NO;` on every UIScrollView ancestor.

**The narrowing matters.** The first version did this in the generic pane
constructor, which would have stopped every scrolled page in the application
scrolling by drag. Doing it only for GL canvas ancestors keeps scrolling
everywhere else. If a future change makes some other view stop responding to
drags, this loop is the first suspect.

**How to confirm:** load a model, drag continuously for 30+ seconds, then read
`orca-ios-canvas:`. `mouse +N` staying non-zero throughout is the fix working.

## wx patches added this session (step2, 0219-0223)

**`0219-iphone-implement-setfont.patch` — the big one.**
`wxWidgetIPhoneImpl::SetFont` had an **empty body**. Every `wxWindow::SetFont`
call in the entire application did nothing; every control kept UIKit's 17 pt
system font, while the layout code sized itself for the fonts it thought it had
set. That single stub is the root of most of the "stretched / clipped /
overlapping text" reports going back weeks. Implemented via
`font.OSXGetCTFont()` → `CTFontCopyPostScriptName` → `[UIFont
fontWithName:size:]`, routing UIButton through `titleLabel`. After it,
Body_10/12/14 measure 66/79/92 px — distinct, and in HarmonyOS Sans SC.

**`0220-iphone-text-field-height-floor.patch`** — `wxUITextFieldControl::
GetBestSize()` floored height at 31 (33 after the `+2`), against Cocoa's ~24.
Lowered to 22.

**`0221-iphone-redraw-panes-on-resize.patch`** — `CreateUserPane` set
`contentMode`/`clipsToBounds`/`clearsContextBeforeDrawing` on `sv`, the
**parent**, and never on `v`, the pane being created. Added
`v.contentMode = UIViewContentModeRedraw;`.

**`0222-iphone-honour-bu-exactfit.patch`** — the condition was
`if ((wBtn > sz.x) || (GetWindowStyle() & wxBU_EXACTFIT))`, i.e. `wxBU_EXACTFIT`
*forced* the 72 pt minimum width it is supposed to waive. Now
`if (sz.x < wBtn && !(GetWindowStyle() & wxBU_EXACTFIT))`.

**`0223-iphone-cancelled-touches.patch`** — see above.

## Orca patches added this session (step3, 0358-0375)

Grouped by what they were for. Numbers are the filenames in `patches/step3/`.

*Text and layout:* 0358 paint the preset combo text; 0359 register the bundled
fonts through CoreText at launch (`orca_ios_register_bundled_fonts()` — iOS
ignores `ATSApplicationFontsPath`, and `UIAppFonts` would not have found
`orca-resources/fonts` either); 0360 log why the Process row is missing; 0361
font metrics + TextInput paint logging; 0362 nozzle `wxGridSizer` →
`wxBoxSizer` plus an Ellipsize guard; 0363 `PRINTER_PANEL_SIZE` 60 → 66 on iOS.

*Rendering:* 0364 render from the paint event, canvas counters, SwitchButton
minimum size; 0370 name the window that blocks rendering; 0373 make the stall
report state its own verdict; 0374 restore the framebuffer that was actually
bound; 0375 count touches reaching the canvas.

*Saving:* 0365 stage 3MF saves outside the picked folder; 0366 autosave into
Documents; 0369 put the autosave into recent projects.

*Printer:* 0367 (three things: `BoostThreadWorker::clear_percent` via
`CallAfter`, Ctrl+S straight to Documents, and enabling HMS on iOS); 0368
surface printer error codes; 0371 build the camera view and drop its stub; 0372
trace the notebook selection.

Two of those deserve their own note.

**0367 / the send dialog hang.** Sending to the printer hung at 75 %. The stack
was `-[UIView setHidden:]` **off the main thread** —
`BoostThreadWorker::clear_percent()` touched the progress dialog directly from
its worker thread. On macOS AppKit tolerates this often enough to go unnoticed;
UIKit does not. The fix marshals through `wxTheApp->CallAfter` on iOS only:

```cpp
void clear_percent() override {
    if (m_progress) {
#if defined(__APPLE__) && !TARGET_OS_OSX
        auto pri = m_progress;
        wxTheApp->CallAfter([pri] { pri->clear_percent(); });
#else
        m_progress->clear_percent();
#endif
    }
}
```

**0367 / HMS.** Printer errors never displayed because HMS was gated on
`installed_networking`, which is false on iOS and always will be — there is no
network plugin to install. But the HMS description tables come from a plain
public HTTPS endpoint (`query.php`) that needs no plugin at all:

```cpp
static bool should_disable_hms() {
    if (!config) return true;
    if (config->get_stealth_mode()) return true;
#if defined(__APPLE__) && !TARGET_OS_OSX
    return false;   // no plugin needed; query.php is public HTTPS
#else
    return !config->get_bool("installed_networking");
#endif
}
```

Confirmed: the tables for device type `039` (A1) **do** download, and error
codes now reach the UI. There is also a bare-code fallback so an unknown code
shows as a code rather than as nothing.

## The camera, over the LAN

Bambu's own client gets the stream URL through a System V shared-memory segment
written by a companion process (`shmget` — which is why 0356 had to stub it out;
SysV IPC raises **SIGSYS** in the iOS sandbox and no handler can decline it).
There is no companion process here, so the protocol is spoken directly.

**Protocol** (`orca-overlay/src/slic3r/Utils/BambuLanCamera.{hpp,cpp}`,
`Slic3r::BambuLan::CameraClient`): TLS on TCP **6000**; the printer's
certificate is self-signed and its handshake is old, so verification is off,
`SSL_CTX_set_security_level(ctx, 0)` and cipher list `"ALL:@SECLEVEL=0"`. Then a
fixed **80-byte** auth frame — `0x40`, `0x3000`, `0`, `0`, `username[32]`,
`access_code[32]`, all little-endian — followed by a stream of **16-byte
headers** (first 4 bytes = payload length) each introducing one complete JPEG.
`MAX_FRAME_SIZE` 8 MB. Callbacks `OnFrameFn`/`OnErrorFn`, plus a frame counter.

**The view** (`orca-overlay/src/slic3r/GUI/ios_camera_view.cpp`) implements
`wxMediaCtrl2` for iOS: parses
`bambu:///local/<ip>.?port=6000&user=bblp&passwd=<code>`, scales frames to fit,
draws status or error text when there is no frame. Wired up in 0371.

**Two failures on the way, both instructive:**

1. Nothing connected, because `Load()` only parsed the URL. `MediaPlayCtrl`
   posts *either* a URL *or* `"<play>"`, never both — so `Play()` was never
   called. `Load()` now calls `Play()` itself.
2. Then it was killed mid-handshake, four times in a row. `GetState()` withheld
   `wxMEDIASTATE_PLAYING` until the first frame arrived; `MediaPlayCtrl` read
   that as "the stream did not come up" and tore the client down before the TLS
   handshake finished. `GetState()` now reports `PLAYING` whenever the client is
   running.

## Licensing, researched and settled

OrcaSlicer is **AGPL-3.0**. Findings the user asked for, so they do not get
re-derived:

* A new native iOS UI on top of this codebase is a derivative work; AGPL applies
  to the whole thing, and the network-use clause applies too.
* Charging money is explicitly fine under the GPL family. **Distribution is the
  problem**: Apple's App Store terms impose usage restrictions the (A)GPL
  forbids adding, which is why VLC was pulled — the resolution there was
  relicensing the engine to **LGPL** (not "LGPLv2.1+/MPL", as I said at one
  point and had to correct).
* App Store distribution is therefore **not** flatly impossible — it needs an
  "additional permission" under GPLv3 §7 from *every* copyright holder, which
  for a project the size of Orca is the real obstacle.
* Free sideloading plus a paid App Store build is not a way around it: the App
  Store copy is a distribution and must satisfy the licence on its own.

## Distribution: releases, SideStore source, notifications

Ported from `Toemeler/ipadprocad`, deliberately the same shape.

**`ci/publish_release.sh`** — publishes on every green run: the IPA, a minimal
IPA (executable + Info.plist, for install diagnosis), `source.json`
(SideStore/AltStore source, last 10 builds) and `latest.json` (flat manifest for
the Shortcut). Tag `orcaslicer-ipa-run<N>`, version `1.0.<N>`, bundle id
`org.orca-ios.orcaslicer`, `minOSVersion 17.0`, last 15 releases kept. Still
non-fatal on 403 — a release failure must not redden a build.

Fixed entry points (GitHub redirects `latest` to the newest release):

```
https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/source.json
https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/latest.json
https://github.com/Toemeler/Orca-iOS-ipa/releases/latest/download/OrcaSlicer-iPad.ipa
```

**`ci/notify_build.sh`** — pushes a notification via Pushover and/or ntfy; every
path exits 0. **Configured and confirmed working**: the repository secret
`NTFY_TOPIC` is set to `orcaslicer-ipad-d663c75ff802` (28 characters — the test
workflow prints the length, never the value), and the user received the
notification on the iPad.

**The notification's click URL is `shortcuts://run-shortcut?name=Install%20OrcaSlicer`
and deliberately NOT the IPA URL.** An ntfy topic is public to anyone who knows
its name, and ntfy's iOS client hands the click URL straight to
`UIApplication.open` without inspecting it — a notification carrying a download
URL would be a stranger's lever for feeding an arbitrary IPA into the install
Shortcut. The Shortcut resolves `latest.json` itself, so a spoofed notification
can at worst offer the genuine newest build. **Do not "simplify" this.**

The iPad Shortcut is named exactly **`Install OrcaSlicer`** (the CI puts that
name in the URL): fetch `latest.json` → take `ipaURL` → VPN on → wait 2 s → open
`sidestore://install?url=…`. Two taps is the floor; SideStore has no install
App Intent and `sidestore://install` always raises its Install/Cancel dialog.
Full write-up in `docs/AUTOINSTALL.md`.

**`.github/workflows/ntfy-test.yml`** — runs `notify_build.sh` on
`ubuntu-latest` and nothing else; **11 seconds** versus a 16-minute device
build. It fires on a push touching its own file (a new workflow is not
dispatchable until it exists on the default branch — it does now, so
`workflow_dispatch` works from `main`). Use it whenever the notification path or
the secrets change.

## The canvas is still wrong (1600x1200)

`UIScreen.bounds` reports 1600x1200 where an iPad16,5 should be 1376x1032 at 2x.
2752/1600 and 2064/1200 are both exactly **1.72**, so everything drawn is
rescaled by a non-integer factor.

**Ruled out by experiment, do not re-test:** `UIApplicationSceneManifest`
(present — `orca-ios-variant: sceneManifest yes` — canvas unmoved); dropping
`UIRequiresFullScreen`; both together. The default `OrcaSlicer-iPad.ipa` is the
`both` variant; the untouched one is kept as `OrcaSlicer-iPad-baseline.ipa`.

**Current untested candidate:** the bundle had **no SDK metadata at all**, which
is one of the documented triggers for iPad compatibility mode. Run 104 added it
to the Info.plist — `DTPlatformVersion 18.5`, `DTSDKName iphoneos18.5`,
`DTSDKBuild 22F76`, `DTXcode 1604`, `DTXcodeBuild 16F6`, `DTCompiler
com.apple.compilers.llvm.clang.1_0`, derived at build time from `xcrun
--show-sdk-version` and `xcodebuild -version`. Verified present in the run-104
IPA by range-fetching the zip's first entry. Whether it moves the canvas is
unknown — read `orca-ios-display` from the next device log. 1376x1032 means
solved; 1600x1200 means it did not.

## Diagnostic logging that should come out

Added to chase the freeze and never removed. Once the user confirms the viewport
is fixed, strip: `orca-ios-canvas:` (0364/0373/0375), `orca-ios-textinput:`
(0361), `orca-ios-notebook:` (0372). The `orca-ios-display`, `orca-ios-variant`,
`orca-ios-font` and `orca-ios-lan` lines are cheap and fire once — keep them.

## Compile failures worth not repeating

* **Run 96:** `wxWindow *const canvas = … : nullptr` with `wxGLCanvas`
  incomplete, and `std::string(w->GetClassInfo()->GetClassName())` — that
  returns `const wxChar*`. Fix: include `<wx/glcanvas.h>`, build the chain as a
  `wxString`.
* **Run 99:** `'this' cannot be implicitly captured` — the counter timer's
  lambda captured `[counters]` while the new code used `m_tabpanel`/`m_plater`.
  Fix: `[this, counters]`.

## Verifying patches before spending an hour of CI

Unchanged, and it catches nearly everything:

```bash
cd scratchpad/full && git reset -q --hard 395e070a0e675fd4723f93967cefede730c482d9 && git clean -qfd
for p in patches/step1/*.patch patches/step3/*.patch; do git apply "$p" || echo FAIL; done
cd scratchpad/wx && git reset -q --hard HEAD && git clean -qfd
for p in patches/step2/*.patch; do git apply "$p" || echo FAIL; done
```

To generate a patch: snapshot the base file **after the earlier patches in the
series have been applied**, edit, then
`diff -u --label a/<path> --label b/<path> base/<f> full/<path> > patch`. A
`git diff` against pristine HEAD produces a patch containing every earlier
change too — that mistake was made once this session with 0223 and cost a round.

## Open, in the order I would take them

1. **Confirm the freeze fix** (0223) on the device. Everything else is smaller.
2. **Read `orca-ios-display`** from that same log for the canvas verdict.
3. **Strip the diagnostic logging** once 1 is confirmed.
4. **The Device page in portrait** — the Control panel lays out to ~1370 pt
   against 1200 available. `StatusBasePanel` is already a `wxScrolledWindow`
   with `wxHSCROLL | wxVSCROLL` and `SetScrollRate(25,25)`, so the scaffolding
   exists; this may simply resolve once touches are delivered correctly, which
   is a reason to do it after 1.
5. **Bambu Studio-style error UI** — severity levels and illustrated recovery
   steps. Feature-sized, and unblocked now that the `039` tables download.
6. **Sweep for other SysV IPC** — `shmget`/`shmat`/`shmctl`/`ftok` are a class,
   not an instance. Silent until reached, then SIGSYS.

## Process notes for whoever is next

* **Wait for the build to finish.** The user asked for this explicitly. Do not
  hand back a build that is still running.
* **Printer credentials are baked into this public repo by the user's explicit,
  reaffirmed decision** (access code, IP and serial, patch 0348). It was flagged
  and reaffirmed. Do not silently remove them; do not add more.
* **The egress proxy in this environment blocks** `ntfy.sh`, `e.bambulab.com`,
  `www.fsf.org`, `appfair.org`, `signal.org` and
  `results-receiver.actions.githubusercontent.com`. A 403 from
  `CONNECT tunnel failed` is the proxy, not the remote host — never work around
  it by disabling TLS verification.
* **`gh` is not available** in this environment; use the GitHub MCP tools. There
  is no way to read repository secrets from here — a workflow that prints their
  *length* is the only honest check.

# SESSION 2026-08-10 (later): the viewport freeze, from a device log that
# contradicts the previous session's answer

## Headline

The Prepare freeze is **not** a touch-delivery bug. It is
`-presentRenderbuffer:` being handed nothing to present, so the canvas renders
every frame and none of them ever reach the glass. Fixed by wx patch **0224**
and Orca patch **0376**. The previous section's conclusion — that 0223
(cancelled touches) was the fix — is not what the device shows, and the reason
is written out below, because reading the counter line wrongly is what has cost
this bug most of its round trips.

## The log, read line by line

User's report: *"I can import a part then move a bit and rotate and suddenly in
prepare the viewport freezes and the preview viewport is just black."* The log
is from run 106's IPA, so 0223, 0374 and 0375 were all in it.

```
13:35:40.945  take_snapshot  Import Objects: 1204 TOKA Base.stl
13:35:44.797  _save_model_to_file  (autosave)
13:35:44.800  render_thumbnail prepare: w 512, h 512, render_fbo 2   <- plate thumb
13:35:44.818  render_thumbnail prepare: GL_FRAMEBUFFER not complete  <- pick thumb
13:35:44.835  orca-ios-autosave: .../Autosave/1204 TOKA Base.3mf ok
13:35:44.837  canvas: render +403 mouse +405     <- healthy
13:35:44.937  select_plate_by_hover_id           <- user still clicking
13:35:45.962  select_plate_by_hover_id           <- and again
13:35:49.138  tabbtn: pressed ' Preview'         <- gives up, goes to Preview
13:35:49.818  canvas: render +284 mouse +262     <- STILL rendering, STILL hearing touches
13:35:50.105  tabbtn: pressed ' Device'
13:35:52.046  tabbtn: pressed ' Prepare'
13:35:52.780  tabbtn: pressed ' Preview'
13:35:54.276  tabbtn: pressed ' Prepare'
13:35:54.818  canvas: render +116 mouse +0
```

**`mouse +262` in the window that spans the freeze is the whole argument.**
Touches were being delivered normally while the viewport was already dead, and
two `select_plate_by_hover_id` lines *after* the autosave prove the application
was acting on them. The `mouse +0` further down is a user who has stopped
dragging a picture that will not move — not a canvas that stopped hearing. Do
not read a zero mouse count as the cause again without checking whether anything
was being touched during that window.

The freeze starts inside the five seconds containing the **first autosave**,
which is the "suddenly, a few seconds after importing" in every report of this
going back weeks. The backup timer is what makes it feel spontaneous.

## Root cause 1 (the freeze): deleting a bound renderbuffer unbinds the drawable

On iOS a wx GL canvas is a `GLKView`. Its colour renderbuffer is what
`-[EAGLContext presentRenderbuffer:]` puts on the screen, and that call presents
**whatever is bound to `GL_RENDERBUFFER` at that instant** — nothing else
identifies the target.

`GLCanvas3D::render_thumbnail_framebuffer` (and `_ext`, and
`_rectangular_selection_picking_pass`) do this:

```cpp
glGenRenderbuffers(1, &render_depth);
glBindRenderbuffer(GL_RENDERBUFFER, render_depth);   // drawable's colour buffer displaced
...
glDeleteRenderbuffers(1, &render_depth);             // binding reverts to 0, per spec
```

After the first plate thumbnail, `GL_RENDERBUFFER_BINDING` is 0 for the rest of
the process. `wxGLCanvas::SwapBuffers()` still runs, `presentRenderbuffer:`
still returns — and does nothing. Every later frame is rendered correctly and
thrown away.

That is both symptoms at once, exactly as reported:

* **Prepare** keeps showing the last frame it managed to present. Frozen.
* **Preview**, which had never presented a frame before the thumbnail ran, has
  never had anything in its layer. Black.

`0374` guarded the *framebuffer* binding around these passes, which was right
and not sufficient — the renderbuffer is the one that mattered.

## Root cause 2 (latent, same symptoms): one drawable for two canvases

`wxGLContext::SetCurrent` bound the canvas's drawable only when it had no size
yet (`drawableWidth == 0`), so after the first frame it never rebound. A
`wxGLContext` is *shared* between canvases — Prepare and Preview are two GLKViews
on one `EAGLContext` — and the bound framebuffer is state of the context, not of
the canvas. So whichever canvas drew last owned the binding, and the other one
drew into it. `orca_ios_note_default_framebuffer()` then sampled that stale name
at the top of `render()` and every `glBindFramebuffer(…, 0)` in the frame
compounded it.

This produces the identical pair — Prepare frozen, Preview black — and would
have surfaced the moment root cause 1 was fixed. Both are fixed here.

## Root cause 3 (visible in the log): `GL_DEPTH_COMPONENT` is not an ES format

```
render_thumbnail prepare: GL_FRAMEBUFFER not complete
```

`glRenderbufferStorage` takes a **sized** internal format and ES has no unsized
depth format, so `GL_DEPTH_COMPONENT` is `GL_INVALID_ENUM` there: no storage,
`INCOMPLETE_ATTACHMENT`. Only the picking thumbnail hits it, because that is the
one branch that disables multisampling — the multisampled branch two lines above
already asks for `GL_DEPTH_COMPONENT24`. Every 3mf this port has written so far
carries a blank pick thumbnail.

## The patches

**`patches/step2/0224-iphone-present-this-canvas-drawable.patch`** —
`src/osx/iphone/glcanvas.mm`:

* `-swapBuffers` does `[self bindDrawable]` before `presentRenderbuffer:`, so
  presentation stops depending on ambient GL state. This is what GLKit's own
  `-display` does; an application that draws outside `-drawRect:` has to do it
  itself.
* `-swapBuffers` returns the `BOOL` from `presentRenderbuffer:`, and
  `wxGLCanvas::SwapBuffers()` returns it instead of a hardcoded `true`.
* `wxGLContext::SetCurrent` binds the drawable **every** time, not only the
  first — root cause 2.

**`patches/step3/0376-ios-offscreen-passes-must-give-the-drawable-back.patch`** —
`GLCanvas3D.cpp`, `MainFrame.cpp`:

* `OrcaIosFramebufferGuard` now saves and restores `GL_RENDERBUFFER_BINDING`
  alongside the framebuffer, and is applied to
  `_rectangular_selection_picking_pass` as well as the two thumbnail paths.
* `ORCA_DEPTH_RENDERBUFFER_FORMAT` — `GL_DEPTH_COMPONENT24` under
  `SLIC3R_OPENGL_ES`, `GL_DEPTH_COMPONENT` elsewhere — at all four
  `glRenderbufferStorage` sites.
* Two counters, `orca_ios_canvas_swaps` and `orca_ios_canvas_swap_failures`, on
  the existing five-second line.

## How to confirm on the device, and what each reading means

The counter line now reads:

```
orca-ios-canvas: last 5s idle +N paint +N render +N mouse +N swap +N fail +N; sel S (plater)
```

* `render +N` with `swap +N` and `fail +0` — frames are reaching the screen.
  A viewport that still looks frozen with this reading is a camera or a scene
  problem, not a presentation one.
* `render +N swap +N fail +N` — presentation is being refused. Back here.
* `render +0` — nothing is drawing; that is the render gate, and the chain dump
  below the counter line names the window responsible.

Repro: import an STL, drag the view, wait for the first autosave (~4 s after the
import; look for `orca-ios-autosave: … ok`), keep dragging. Then switch to
Preview and back.

## Corrections to the section above this one

* **"It was never a rendering bug — the touches stopped arriving."** Not in this
  log. Touches arrive throughout the freeze; `mouse +262` covers the moment it
  starts. 0223 is kept — cancelled touches genuinely were unhandled, and the
  scroll-view ancestors genuinely can steal a drag — but it is not the fix for
  the freeze the user keeps reporting.
* **"Wrong theory 2 — the framebuffer. It was not the freeze."** It was the
  freeze; 0374 simply guarded the wrong binding of the two. Its own comment
  predicted the exact symptom pair.

## Open, in the order I would take them

1. **Confirm 0224 + 0376 on the device** — import, drag past the first autosave,
   then Preview and back. Read `swap`/`fail` on the counter line.
2. **Read `orca-ios-display`** from that same log for the canvas verdict; run
   106 still reported 1600x1200, so the SDK-metadata guess has not landed yet.
3. **Strip the diagnostic logging** once 1 is confirmed — now including the
   `swap`/`fail` counters.
4. **The Device page in portrait** — the Control panel lays out to ~1370 pt
   against 1200 available. `StatusBasePanel` is already a `wxScrolledWindow`
   with `wxHSCROLL | wxVSCROLL` and `SetScrollRate(25,25)`, so the scaffolding
   exists.
5. **Bambu Studio-style error UI** — severity levels and illustrated recovery
   steps. Feature-sized, and unblocked now that the `039` tables download.
6. **Sweep for other SysV IPC** — `shmget`/`shmat`/`shmctl`/`ftok` are a class,
   not an instance. Silent until reached, then SIGSYS.

# SESSION 2026-08-10 (later still): MakerWorld deep links, and the canvas
# experiment closed out

## The canvas question is answered, and the answer is no

Open item 2 from the section above is settled by the same device log. Run 106
shipped the **`both`** arm as the default IPA, and it reported:

```
orca-ios-variant: sceneManifest yes, UIRequiresFullScreen no
orca-ios-display (MainFrame): UIScreen.bounds 1600x1200, nativeBounds 2064x2752,
                              scale 2, nativeScale 1.72
```

Identical to the baseline. With `scene` already dead in run 82, **all three arms
are ruled out**: `UIApplicationSceneManifest`, dropping `UIRequiresFullScreen`,
and both together. The 1600x1200 canvas is not a bundle key. Do not spend
another round on Info.plist for it; whatever is next has to come from how wx
creates its `UIWindow` (`src/osx/iphone/nonownedwnd.mm`), which builds one
directly against `UIScreen` with no `UIWindowScene` at all.

**The default IPA is the baseline again**, and that is now load-bearing rather
than tidy — see below.

## MakerWorld "Open in Bambu Studio"

The button navigates to `bambustudio://open?file=<3mf url>&name=<name>` (older
pages use `bambustudioopen://<url>`). Orca's `Downloader::start_download`
already accepts both, plus `orcaslicer://`, `prusaslicer://` and `cura://`, and
routes a Bambu-style link to `Plater::request_model_download` →
`import_model_id`, which downloads the 3mf and loads it. **None of that needed
changing.** What was missing was every step before it.

**1. The bundle never claimed the schemes.** No `CFBundleURLTypes`, so iOS did
not know the app handles them and Safari called the address invalid. Added to
the Info.plist in `ios-step4-device-ipa.yml`, with a verify assertion next to
the icon and dSYM ones, because a lost URL type fails in a way nobody would
trace back to the bundle.

**2. wx's iPhone port never received a URL.** `wxAppDelegate` in
`src/osx/iphone/utils.mm` implements `willFinishLaunching`,
`applicationDidFinishLaunching` and `applicationWillTerminate` and nothing else;
there is no `-application:openURL:options:`, so the callback UIKit makes went
nowhere. `wxApp::MacOpenURL` — the same entry point the macOS port reaches
through an Apple Event, and the one `GUI_App` already overrides — was
unreachable on iOS. **`0225-iphone-deliver-open-url.patch`.**

Two traps in that patch worth keeping:

* **A cold start delivers the URL before OnInit.** `0208` defers
  `OSXOnDidFinishLaunching()` to the next run-loop turn, so a link that launches
  the app arrives while there is no `Plater`. The URL is held and delivered
  after OnInit returns.
* **iOS announces a launch URL twice** — once in `launchOptions`, once through
  `-application:openURL:options:` after launching finishes. Acting on both
  downloads the model twice. One slot, replaced rather than queued, collapses
  the pair.

**3. `UIApplicationSceneManifest` would have broken it anyway.** With a scene
manifest, URL delivery moves to `-scene:openURLContexts:` on a scene delegate,
and wx 3.3.2's iPhone port has no `UIScene` support at all — so the link would
be handed to a delegate that does not exist. That is why the default IPA going
back to the baseline is load-bearing, and why the workflow now asserts the
default has no scene manifest.

**4. There was nowhere to download to.** `wxStandardPathsBase::GetUserDir`
ignores its argument and returns the home directory, and there is no iPhone
override for `Dir_Downloads`, so `download_path` pointed at the app container
root. Both `Downloader::start_download` and `Plater::import_model_id` give up
when it is not an existing directory, so the link would have failed with
"Destination folder is not set". `init_download_path()` now uses
`$HOME/Documents/Downloads` on iOS and creates it — `Documents/` because that is
the only directory the bundle exports, through `UIFileSharingEnabled` and
`LSSupportsOpeningDocumentsInPlace`, so downloaded models show up in Files.

**`0377-ios-makerworld-open-in-slicer-deep-links.patch`** carries 2 and 4, plus
an `orca-ios-deeplink:` log line at error level — whether the link reached the
application at all is the first thing anyone will want to know — and a guard
that ignores non-slicer URLs instead of reporting them as malformed.

## What this does NOT do

Opening a local `.3mf` from Files or from a Safari download. That arrives as a
`file://` URL and is deliberately ignored with a log line rather than shown as a
malformed link: reading a file outside the container needs
`startAccessingSecurityScopedResource`, and it wants `CFBundleDocumentTypes` and
`UTExportedTypeDeclarations` in the bundle to appear in the share sheet at all.
Separate piece of work, and worth doing.

## How to test it

Safari on the iPad → any MakerWorld model page → **Open in Bambu Studio**. iOS
should offer to open OrcaSlicer. Then read the log:

```
orca-ios-deeplink: download_path /var/mobile/.../Documents/Downloads
orca-ios-deeplink: bambustudio://open?file=...
```

Both lines present and the model loads = working. First line only = the link
never reached the app, which is the bundle (schemes) or wx (delivery). Second
line followed by "not a slicer open protocol" = the button sent something the
regex in `Downloader::start_download` does not match, and that regex is the
place to look.

Try it twice: once with OrcaSlicer **not** running (cold start, the launch-URL
path) and once with it already open (the `openURL` path). They are different
code paths and only the cold one goes through the deferred delivery.

## Correction: ccache on step 4 is not broken, and main's cache scope was empty

Written down because I got this wrong once in this session and the same wrong
conclusion is one glance at a slow run away.

**The claim "ccache is missing on step 4" is out of date.** It dates from run
26. The PCH was turned off afterwards and the published reports from runs 96 and
99 — both on `claude/second-startup-crash-k0162l`, 2026-08-08/10 — say:

```
635 Result: direct_cache_hit
  3 Result: direct_cache_miss
```

99.8%. That is why `[7/9] Build + link` on run 106 took **0.1 min** and the
whole run took 7 minutes.

**Run 108 took 113 minutes for an unrelated reason: `main` had no caches.**
Its steps read

```
[3/9] Restore device deps cache    0.0 min   <- MISS
[4/9] Build deps                  26.4 min
Restore wx prefix cache            0.0 min   <- MISS
[5/9] Build wxWidgets              5.0 min
Restore ccache                     0.0 min   <- MISS
[7/9] Build + link                75.9 min
```

Three straight misses. GitHub Actions caches are readable only from the branch
that wrote them and from the default branch, and every step-4 run from 84 to 106
ran on `claude/second-startup-crash-k0162l`, so all of it — deps, wx prefix,
ccache — lived in that branch's scope and none of it was visible to `main`. The
workflow file is byte-identical between the two; **nothing was missing from
`main` except cache data, which is not in git**. Verified with
`git diff <branch> main -- .github/workflows/ios-step4-device-ipa.yml`: empty.

Run 108 paid that bill once and populated all three on `main`. Because `main`
is the default branch, every branch now inherits them.

**Reading a slow run correctly.** The three restore steps are the diagnosis, in
this order:

| Reading | Meaning |
|---|---|
| `Restore ccache` 0.0 min | no entry in this branch's scope — cold, and nothing is wrong |
| restores hit, `[7/9]` still long | a widely-included header changed, or `ccache -s` shows real misses |
| restores hit, `[7/9]` ~0.1 min | fully warm |

A header change is a legitimate long build with a warm cache: `GUI_App.hpp` and
anything else included across `libslic3r_gui` invalidates a large slice of the
639 objects on its own.

## The ccache on step 4 is instrumented now, because durations cannot answer it

`Restore ccache` read **0.0 min on run 108 and again on run 109**, and that
number cannot distinguish the two cases that matter. A populated ~640-object
ccache restores in about six seconds (run 106: 0.1 min, 635 hits) and an absent
one in zero. Both round to something indistinguishable, and in this environment
there is no second opinion: **job log downloads and the Actions cache API are
both blocked by the egress proxy** (`/actions/runs/<id>/logs` and
`/actions/caches` return 403 `Access to this GitHub Actions path is not
permitted through this proxy`). So a green run has left no ccache statistic
behind, ever — the publisher is `if: failure()`, and the step that prints
`ccache -s` prints it somewhere unreadable.

Three changes, so the next run answers this outright instead of being inferred:

* **`ccache restore report`** records `cache-hit`, `cache-matched-key`, the
  directory size and the file count *before* anything compiles.
  `cache-matched-key` names the entry that was served, or is empty when there
  was none. That is the fact that has been missing.
* **`Publish ccache stats to repo (ci-logs/)`**, `if: success()`, commits that
  plus `ccache -s -v` and the ranked results to
  `ci-logs/step4-run-N/ccache-stats.txt`. Separate step from the failure
  publisher, so the two can never race to commit.
* **`Save ccache` is now gated on the build step having run**
  (`steps.orcabuild.outcome != 'skipped'`).

That last one is a bug with evidence, not a precaution. The concurrency group
cancels the running build whenever a new one is dispatched, `if: always()` runs
on cancellation, and a run cancelled before `[7/9]` therefore **saves an empty
ccache under a brand new key**. Restore matches the prefix `ccache-step4-` and
takes the most recent entry, so that empty one outranks every good cache on the
branch. Run 107 was cancelled 21 minutes into the deps step and its `Save
ccache` reported success in 0.0 min — that is exactly the shape. It did not
cause run 108's cold build (main had no ccache at all either way), but on a
branch with a warm cache it would silently cost an hour, and the same trap for
the *deps* cache is already documented further up this file.

**Still open:** whether run 108's ccache save actually landed. The candidate
that does not need the cache API is the **10 GB repository cache quota** — the
device deps entry is multi-GB, it is written on every run, several branches have
their own copy, and `actions/cache/save` warns rather than fails when a save is
rejected, which shows up as a green step. `ccache-stats.txt` from the next green
run settles it: a `cache-matched-key` naming run 109's entry means saves land
and something else is wrong; an empty one means they do not.

## ccache on step 4, settled with numbers (run 110)

`ci-logs/step4-run-110/` now carries the report, and it closes the "still open"
question above: **saves land, restores work, and `main` is at parity with the
old branch.**

```
cache-hit:         false
cache-matched-key: ccache-step4-31406517823      <- run 109's entry
dir:               96M, 1715 files
```

`cache-hit: false` is not a miss. The key is `ccache-step4-<run_id>` and is
unique per run by design, so an exact hit is impossible; the entry always
arrives through the `ccache-step4-` restore-key prefix, and
**`cache-matched-key` is the only field that says whether anything came back.**

ccache's own cumulative counters, read across the three runs:

| Run | calls | hits | misses | `[7/9]` |
|---|---|---|---|---|
| 108 | 639 | 0 | 639 | 75.9 min |
| 109 | 639 | 492 (77%) | 147 | 41.9 min |
| 110 | 639 | **639 (100%)** | 0 | **0.2 min** |

**Run 109 was 77% cached, not the ~50% its wall time suggests.** The 147 misses
were the expensive translation units — `GUI_App.cpp`, `Plater.cpp`,
`GLCanvas3D.cpp` are each enormous — and `[7/9]` also links a 146 MB binary,
which ccache does not cache at all. Do not convert `[7/9]` minutes into a hit
rate; they are not proportional.

The cache is 96 MB against a 1.5 G limit (6%), so eviction pressure inside
ccache is not a factor, and the 10 GB repository quota theory for run 108 is
dead: run 109's entry was saved and served.

**`Restore ccache` duration carries no information.** It read 0.0 min on run 108
(total miss, 76 minutes of compiling) and 0.0 min on run 110 (100% hit, twelve
seconds). Read `ci-logs/step4-run-N/ccache-restore.txt` instead.

## The run-109 release failure was a one-off

Run 109 was green and published nothing; run 110, same code path and same
dispatcher, published normally and pruned run 90 as designed. No explanation,
and the job log that had one is not reachable through the proxy. The publish
output is now teed into `ci-logs/step4-run-N/publish-release.log` on green runs,
so a recurrence names itself instead of costing another build. **Check that a
release actually exists before telling anyone a build is ready** — a green run
is not sufficient evidence, which is the whole lesson here.

## The launch SIGSEGV: a member added to GUI_App.hpp

**Read this before adding anything to `GUI_App.hpp`.**

Run 110 and 111 crashed at launch, three times each, signal 11. The first three
crash reports were useless — the unwinder could not get past `GUI_Run`, printing
`GUI_Run + 136` twice and stopping — and the device log ended after three lines,
the last of which was new in that build. That combination sent me after two
wrong theories in a row: first the working directory (`orca-ios-cwd` had changed
its verdict for the first time ever), then the Info.plist (the default IPA had
just gone back to the baseline, and every known-good build had shipped the
`both` variant). **Both wrong.** The breadcrumbs added in 0378 produced a
complete backtrace on the next launch:

```
AppConfig::version_check_url() const + 96        <- SIGSEGV
PresetUpdater::priv::priv() + 440
PresetUpdater::PresetUpdater() + 32
GUI_App::on_init_inner() + 7680
GUI_App::OnInit() + 220
-[wxAppDelegate applicationDidFinishLaunching:]_block_invoke
```

`PresetUpdater::priv::priv()` calls
`set_download_prefs(GUI::wxGetApp().app_config)`, and `set_download_prefs`
dereferences that pointer. The pointer was garbage.

**Cause.** Patch 0377 added a member to `GUI_App`:

```cpp
private:
    bool            m_initialized { false };
    bool            m_post_initialized { false };
#if defined(__APPLE__) && !TARGET_OS_OSX
    wxString        m_ios_pending_open_url;      // line 252
#endif
```

`app_config` is declared at **line 693 of the same class**. Inserting a member
above it moves its offset, and `GUI_App.hpp` is included by several hundred
translation units. Any one of them that did not agree about the new layout read
`app_config` from the old offset and got whatever happened to be there. Nothing
about the crash pointed anywhere near deep links, which is exactly what makes
this class of bug expensive.

I could not prove *which* translation unit disagreed or why — no `SYSTEM`
include directory covers `src/slic3r`, so ccache ought to have invalidated every
object that includes the header, and job logs are not readable here. It does not
matter: the design was wrong regardless.

**Fix (0379): the holder is a file-static in `GUI_App.cpp`.** `GUI_App.hpp` is
now byte-identical to upstream again — verified with
`diff GUI_App.hpp <(git show <pinned-ref>:src/slic3r/GUI/GUI_App.hpp)`, which is
worth running after any patch that touches it.

**Rules that follow:**

* **Do not add members to `GUI_App`** (or any widely-included class) for
  iOS-only state. Use a file-static in the `.cpp`. A conditionally compiled
  member is worse still, because the condition has to be visible identically in
  every translation unit that includes the header.
* **A launch crash whose backtrace stops at `GUI_Run` is not information.** Add
  breadcrumbs and get a real one. 0378 costs one log line per milestone and
  turned three wrong theories into a five-minute answer.
* **The plist variants are innocent by construction** for anything that crashes
  inside `on_init_inner`: all four IPAs come out of **one compile** and share the
  binary. Only launch-time UIKit behaviour can differ between them.

## ⚠ ROOT CAUSE BEHIND THE LAUNCH CRASH: ccache ignored every Orca header

The member added to `GUI_App.hpp` was the trigger. **This is why it was able to
do damage**, and it is a build-correctness bug that predates it and would have
bitten any header change.

Orca's root `CMakeLists.txt`:

```cmake
set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(SYSTEM ${LIBDIR})
```

`src/` is a **SYSTEM** include directory, so `#include "slic3r/GUI/GUI_App.hpp"`
resolves through `-isystem` and every OrcaSlicer header is, to the compiler and
therefore to ccache, a *system header*.

All three workflows set:

```
CCACHE_SLOPPINESS=...,system_headers
```

`system_headers` means **do not hash system headers when computing the cache
key**. Together those two facts mean: **editing any header under `src/` changed
no cache key at all.** Only translation units whose own `.cpp` changed were
recompiled; every other object came back from the cache still compiled against
the *previous* version of the header, and the link put both in one binary.

That is exactly what happened. `GUI_App.cpp` changed, so it recompiled with the
new `GUI_App` layout. `PresetUpdater.cpp` did not change, so its object came
back from run 108's cache with the old layout, read `app_config` from the old
offset, and handed a garbage `AppConfig*` to `version_check_url()`.

It also explains the run-109 numbers that looked odd at the time: **147 misses
out of 639** for a commit that edited a header included across the GUI. 147 is
the count of changed `.cpp` files and their command-line neighbours — the header
dependents never missed, because the header was invisible.

**Fixed:** `system_headers` removed from `CCACHE_SLOPPINESS` in
`ios-step3-gui.yml`, `ios-step3-fast.yml` and `ios-step4-device-ipa.yml`. The
SDK now gets hashed, which is what the setting was avoiding; a runner image
whose SDK changes will invalidate the cache wholesale. That is correct, and far
cheaper than shipping a binary assembled from two different definitions of a
class.

**Consequences worth knowing:**

* **Every green build before this change is suspect if its commit edited a
  header.** Most changes in this port are `.cpp`-only or add new files, which is
  why this survived so long. But a "the fix didn't work on device" report from
  any earlier session could have been this and nothing else.
* **Run 112 is safe by accident**, and it is worth understanding why. 0379
  returned `GUI_App.hpp` to byte-identical with upstream, so the cached objects
  from run 108 — compiled against exactly that header — agree with the freshly
  compiled `GUI_App.cpp`. Everything in that binary shares one layout.
* The first run after this change pays a full recompile, because every cache key
  moves. One ~75 minute run, once.

## Run 112 proved the ccache theory by crashing again, the other way round

I claimed run 112 was "safe by accident" because 0379 restored `GUI_App.hpp` to
upstream and run 108's cached objects were compiled against exactly that header.
**That was wrong**, and the way it was wrong is the cleanest possible
demonstration of the `system_headers` bug.

The breadcrumbs from 0378 put the crash between two adjacent log lines, with one
statement between them:

```
orca-ios-boot: GUI_Run: GUI_App constructed
        bool gui_single_instance_setting = gui->app_config->get("app", "single_instance") == "true";   <- SIGSEGV, GUI_Run + 536
orca-ios-boot: GUI_Run: instance_check        (never printed)
```

`gui->app_config` again, read from the wrong offset — but this time the stale
object is **`GUI_Init.cpp`**, and the direction is reversed:

| object | compiled in | against | `app_config` at |
|---|---|---|---|
| `GUI_Init.cpp.o` | run 111 (served from cache in 112) | 0377 header, **with** the extra member | offset + sizeof(wxString) |
| `GUI_App.cpp.o` | run 112 (its `.cpp` changed) | upstream header, **without** it | offset |

`GUI_Init.cpp` was last edited by 0378 in run 111, so its content — and
therefore its cache key, with the header invisible — was identical in run 112.
ccache handed back run 111's object. Reverting the header could not help,
because the poisoned object had already been *written* under a key that does not
mention the header.

**This is why bumping the key matters and removing the sloppiness alone does
not.** The existing `ccache-step4-*` entries contain objects built against two
different definitions of `GUI_App`, and the prefix restore-key will happily
serve them. The keys are now `ccache-step4-v2-` / `ccache-step3-v2-`, so the
poisoned generation is abandoned rather than reasoned about.

**Method note.** Two consecutive builds failed at launch for the same underlying
reason and I mis-diagnosed the second one *while holding the correct general
theory*, because I reasoned about which objects would be stale instead of
removing the possibility. When a cache can serve a wrong answer, throw the cache
away; do not model it.

## Where the launch crash stands (run 114 published, untested on device)

**Run 114 is the first build carrying the fix that a device can actually
install.** Runs 112 and 113 both failed to reach the iPad — 112 predated 0379,
and 113 built it correctly and then lost the release to a 403. Three device
round trips were spent on builds that could not have shown anything, which is
the expensive lesson of this stretch.

**Run 113 was genuinely cold** — `ci-logs/step4-run-113/ccache-restore.txt`
reads `cache-matched-key:` empty, `files: 0`, and `[7/9]` took 80.4 min. Run
114 changed no C++ at all (workflow, plist and publish script only), so its
binary is assembled from run 113's freshly compiled objects. Testing 114 is the
clean-build test, with no stale-object possibility anywhere in it.

**If run 114 still crashes in `GUI_Run + 536`, the layout theory is dead.** The
next step is not another theory: log the pointers. `(void*)this` and
`(void*)app_config` at the end of `init_app_config()`, in `init_download_path()`
and in `GUI_Run` before the dereference. If the constructor's `this` and
`GUI_Run`'s `gui` differ, or `app_config` is non-null in one and null in the
other, that is the answer in one round.

### Three process holes closed

* **A build could not say which build it was.** `CFBundleVersion` was hardcoded
  `1` and `CFBundleShortVersionString` `1.0`, in every build ever made here, so
  neither the log nor the `.ips` identified it. Both now carry the CI run
  number. A crash report from run 112 arrived while run 113 was still
  compiling and there was no way to tell from the report itself — that is what
  this cost.
* **A failed release looked exactly like a successful one.** `[9/9]` swallows
  its own failure on purpose (correct — the IPA is a workflow artifact either
  way), but the reason went to a job log, which cannot be downloaded through
  this environment's proxy. It is now teed into
  `ci-logs/step4-run-N/publish-release.log` on green runs. That is how run 113's
  `HTTP 403: Resource not accessible by integration` was finally read.
* **That 403 is intermittent and now retried.** Runs 109 and 113 hit it; 110,
  111, 112 and 114 did not, with the same token, dispatcher and permissions.
  `ci/publish_release.sh` now tries four times with backoff, deleting any
  partial release between attempts.

### Method notes worth keeping

* **Ask for the `.ips`, not `orcacrash.txt`.** The app's own handler unwinds
  badly — it printed `GUI_Run + 136` twice and stopped, three times running.
  Apple's `.ips` gave a symbolicated frame (`AppConfig::get(...) const + 48`),
  the exception type and the fault address (`KERN_INVALID_ADDRESS at 0x10`,
  i.e. a null `this` plus 16). One `.ips` was worth more than four
  `orcacrash.txt` files.
* **Verify the release exists before saying a build is ready.** A green run is
  not evidence. `curl .../releases/tags/orcaslicer-ipa-runN` is.
* **Checked and clear:** `Notebook.hpp`'s three `TARGET_OS_OSX` guards all wrap
  code inside method bodies, not member declarations, so they cannot shift a
  layout. No second hazard of that kind in the tree.

# SESSION 2026-08-10 (night): touch and Pencil, and the number keyboard

Run 114 was confirmed on the device: the viewport freeze is fixed, the launch
crash is gone, and MakerWorld's "Open in Bambu Studio" works from both a cold
start and a running app.

## ▲ `EnableTouchEvents` was a stub, exactly like `SetFont`

`include/wx/osx/iphone/private.h`:

```cpp
bool EnableTouchEvents(int WXUNUSED(eventsMask)) { return false; }
```

One line, returns false. Every `wxEVT_GESTURE_*` handler in every application
built against this port has therefore been dead code.

OrcaSlicer is not missing touch support. `GLCanvas3D::bind_event_handlers()`
already binds `wxEVT_GESTURE_PAN`, `wxEVT_GESTURE_ZOOM` and
`wxEVT_GESTURE_ROTATE`, and `GLCanvas3D::on_gesture()` implements all three
against the camera — pan moves the target, pinch multiplies the zoom latched at
gesture start, rotation drives `rotate_on_sphere`. **None of it has ever run on
an iPad.** This is the same shape as patch 0219: the application was right and
the port's stub was silently eating it.

`0226-iphone-implement-enabletouchevents.patch` implements it: pinch →
`wxZoomGestureEvent`, rotation → `wxRotateGestureEvent`, a **two-finger**
`UIPanGestureRecognizer` → `wxPanGestureEvent`, long press → `wxLongPressEvent`.
Recognizers are tracked in an associated object so a second call replaces rather
than stacks them, and the four selectors are added to the view's own class when
missing — without that, UIKit's target dispatch aborts the process with
"unrecognized selector", which is the trap the scroll and hover recognizers
already document.

### The flow, and why it holds together

* **One finger** stays the pointer: orbit on empty space, drag a gizmo, tap to
  select. Unchanged.
* **Two fingers** pan, pinch and rotate. The pan recognizer sets
  `minimumNumberOfTouches = 2` precisely so it cannot compete with the pointer.
* **Long press** raises the object context menu — on a tablet there is no second
  mouse button, and delete, duplicate, "fill bed with copies" and per-object
  settings all live in that menu.
* **Apple Pencil** needs nothing new: it arrives as an ordinary touch (so it is
  the pointer, precisely), and hover already works through the
  `UIHoverGestureRecognizer` added earlier, which is what drives Orca's toolbar
  and gizmo highlighting.

**The gesture and pointer paths cooperate through patch 0223.** The new
recognizers keep `cancelsTouchesInView = YES`, so when a gesture recognises,
UIKit cancels the in-flight touches and 0223 turns that into a clean `LEFT_UP`.
An orbit in progress therefore ends properly instead of continuing underneath
the pinch. The long press relies on the same thing in the other direction: the
cancel completes the tap that was already under way, so the object is selected
before its menu opens — the order a desktop right click happens in — and no
stray release arrives when the finger lifts. A press that fails because the
finger moved cancels nothing, so dragging still orbits.

`0380-ios-touch-gestures-and-long-press-menu.patch` asks for the full mask on
iOS (pan was bound but never requested) and maps `wxEVT_LONG_PRESS` into
`on_mouse` as a synthesised right click, so every decision about which menu
opens stays in the one place that already makes it.

## The number keyboard

`0381-ios-number-keyboard-for-numeric-fields.patch`.
**`UIKeyboardTypeDecimalPad`** — the compact 4x3 keypad.
`UIKeyboardTypeNumbersAndPunctuation` was tried first and rejected by the user
on sight: it is still a full-width keyboard on an iPad, and entering a layer
height should not cover half the plate.

The keypad has only digits and the decimal separator, so an input accessory bar
supplies the rest: **+/-** (sign toggle, for positions and offsets), **%** (for
`coFloatOrPercent`), **,** (for `coPoints` like `0.4,0.6`, and for locales where
the keypad offers a comma but the parser wants a point) and **Done** — which is
not optional, because the decimal pad has no return key and the keyboard could
otherwise never be dismissed. Every edit goes through UITextInput
(`-insertText:`, `-replaceRange:withText:`) so the editing-changed notification
fires and wxTextCtrl sees the value.

The accessory target is owned by the field through `objc_setAssociatedObject`,
because a UIToolbar holds only a weak reference to its target. **This file is
built without ARC** — there is no `-fobjc-arc` anywhere in the CMake — so the
retain/release in it is required.

The helper walks down to the first `UITextField` rather than assuming the handle
is one, because Orca's numeric fields are composites (`::TextInput`,
`::SpinInput`, `::TempInput`), and it is wired into `TextCtrl::BUILD()` and
`SpinCtrl::BUILD()` gated on the numeric `ConfigOptionType`s.

## Two rules applied from the crash earlier today

* `GLCanvas3D::on_long_press` is declared **unconditionally** in the header even
  though only iOS binds it. `GLCanvas3D.hpp` has no `TargetConditionals.h`, so a
  `TARGET_OS_*` test there would read as defined in some translation units and
  zero in others — the exact divergence that cost an evening. A declaration
  nobody calls costs nothing.
* `Field.cpp` gained `#include <TargetConditionals.h>` because it now tests
  `TARGET_OS_OSX`; without it the guard is true on macOS and the iOS-only call
  fails to link.

## Diagnostics: KEPT, on the user's instruction

The plan was to strip the `orca-ios-canvas` counters, the boot breadcrumbs and
the notebook/textinput tracing. The user asked to keep them while more bugs are
being found, and they have earned it — `swap +N fail +0` is what confirmed the
freeze fix, and the boot breadcrumbs turned a three-round launch-crash mystery
into a five-minute answer. Strip them when the port stops changing, not before.

## Compile failures worth not repeating (run 117)

`ios_webview_support.mm`, both from the number-keypad accessory:

* **`error: Objective-C declarations may only appear in global scope`** — the
  `@interface`/`@implementation` were written inside
  `namespace Slic3r { namespace GUI {`, which is where everything else in that
  file lives. Objective-C class declarations cannot go in a C++ namespace. They
  now sit above it; only the exported C++ function stays inside.
* **`error: member access into incomplete type 'wxWindow'`** — the file reaches
  `wxWindow` only through `wx/utils.h`, which forward-declares it, so
  `win->GetHandle()` did not compile. Added `#include <wx/window.h>`.

Both are ordinary and both cost a **50-minute build**, because the failure was
in one late translation unit. Before adding Objective-C to a file that is mostly
C++, check which side of the namespace it lands on.

## ▲ On iPad, `UIKeyboardTypeDecimalPad` is NOT a small keypad

Worth knowing before anyone tries this again. Setting `keyboardType` on a
`UITextField` **cannot** give you the compact 4x3 pad on an iPad — iPadOS
renders both `UIKeyboardTypeNumberPad` and `UIKeyboardTypeDecimalPad` as a
full-width keyboard. That is a platform behaviour, not a bug in the port.

The device log settled it in one round, which is the point of having
instrumented it rather than guessing a third time:

```
orca-ios-keyboard: build: decimal pad set     x65, zero failures
```

The type was being applied to every field, and the full keyboard came up anyway.

**The fix is to supply the keyboard**: `OrcaNumericKeypad` is a `UIView` with
sixteen keys assigned to `field.inputView`, so iOS shows it in place of the
system keyboard. Roughly half the height of the alphabetic one.

    7 8 9 <-      backspace
    4 5 6  -      minus, for positions and offsets
    1 2 3  .      decimal point
    0 , % OK      comma for coPoints, percent for coFloatOrPercent

`OK` lives on the pad, so there is no `inputAccessoryView` any more — the system
decimal pad having no return key was the only reason for one. Key presses go
through `-insertText:` / `-deleteBackward:` and then raise
`UIControlEventEditingChanged` by hand, because a programmatic edit does not
raise it and `wxTextCtrl` listens for exactly that.

## ⚠ OPEN AND UNDIAGNOSED: slicing differs from the desktop

Reported after run 119: *"every slice is kind of fucked up ... some models are
only partly or just weird sliced"*. **This is the most important open item.**

Ruled out already:

* **Not an error path.** The log has no exception, no cancel, no failed step:
  `thread_proc: process finished, state 4, print cancel_status 0`. It completes
  and produces wrong geometry.
* **Not our patches.** Every `patches/step1` patch is build plumbing — the only
  two touching `libslic3r` are `utils.cpp` (executable path) and
  `GCodeSender.cpp` (IOKit). Nothing touches geometry or the slicing pipeline.
* **Not fast-math.** The device build is a plain `-DCMAKE_BUILD_TYPE=Release`;
  there is no `-ffast-math`, `-Ofast` or `-ffp-contract` anywhere.

### ▲ Leading explanation: the iPad is set up for a 0.2 mm nozzle

The device log says what it is slicing with:

```
select_preset: machine ... name Bambu Lab A1 0.2 nozzle
select_preset: process ... name 0.10mm Standard @BBL A1 0.2 nozzle
select_preset: filament ... name Bambu PLA Basic @BBL A1 0.2 nozzle
nozzle_diameter":"0.2"
```

Patch 0348 pre-configures the A1 for the LAN connection but never picks a
**nozzle variant**, so Orca took the first A1 profile in the list — the 0.2 one
(`m_idx_selected 1`). An A1 ships with a 0.4 mm nozzle. Slicing a model for 0.2
at 0.10 mm layers halves every extrusion width: thin walls are dropped, small
features do not generate, regions come out sparse — "only partly or just weird
sliced", with no error anywhere and a clean `state 4` finish.

**Check this before investigating anything else**, and if it is the cause, make
the pre-configuration select the 0.4 variant rather than whatever sorts first.

### Ruled out, with the evidence — do not re-run these

* **Worker thread stack size.** I suggested it; it is wrong.
  `libslic3r/Thread.hpp` sets **16 MB** explicitly in `create_thread`, with a
  comment describing a CGAL `march_locate_2D` recursion that crashed at 4 MB.
  The background slicing thread goes through that helper.
* **Locale / decimal separator.** Plausible on the face of it — the user is in a
  Swiss locale and the log carries `Cannot set locale to language "English
  (Switzerland)"` — but `GCode.cpp` wraps export in `CNumericLocalesSetter`
  (lines 2040 and 3738), so numeric formatting is guarded. TBB workers get
  `newlocale(LC_ALL_MASK, "C")` on top of that.

### Still open if the nozzle is not it

1. **Memory pressure.** An earlier log recorded 1.06 GB resident, 1.2 GB peak.
2. **A dependency built differently for the device slice** (`ORCA_DEPS_GUI=OFF`,
   `CMAKE_DISABLE_FIND_PACKAGE_JPEG=ON`) — confirm CGAL/Boost/TBB take the same
   code paths as the desktop.

**What would settle it fastest:** the same model sliced on PC and on the iPad
with identical settings, plus both G-code files (or the exported 3mf). That
turns "weird" into a diff. A screenshot of the Preview showing the bad region
would narrow it to a stage - missing infill vs missing perimeters vs missing
layers each point somewhere different.

## The numeric keypad, second cut: real Liquid Glass under the field

Run 120 shipped the keypad as `field.inputView`. That works, but UIKit docks an
inputView to the bottom of the screen no matter how small it is, so the keys sat
a long way from the field they edit. The follow-up asked for two things: the pad
under the input field, and native Liquid Glass — "actual native iOS 26 / 27
Liquid Glass, change the whole app to iOS 26 if necessary".

### What the pad is now

`OrcaNumericKeypad` in `patches/step3/0381-…` no longer goes through
`inputView`. The field gets an **empty** `inputView` — a zero-size `UIView`,
which is the supported way to say "first responder, caret, selection, edit menu,
but no system keyboard" — plus emptied `inputAssistantItem` bar groups so
iPadOS does not float its shortcuts bar. The pad itself is added to the
`UIWindow`, positioned with its leading edge on the field's and its top 6pt
below the field's bottom, flipping above the field when there is no room below.
A tap recogniser on the window (`cancelsTouchesInView = NO`, so it only
observes) ends the edit when something else is touched; `OK` and the
begin/end-editing notifications do the rest.

### The API facts worth not rediscovering

Checked against Apple's docs rather than guessed, because a wrong selector costs
a 50-minute build:

* `UIGlassEffect` is `c:objc(cs)UIGlassEffect` — a real ObjC class, iOS 26.0,
  inherits `UIVisualEffect`. `[[UIGlassEffect alloc] init]` is the regular
  style.
* Its interactive flag is `c:objc(cs)UIGlassEffect(py)interactive`, so from ObjC
  it is `glass.interactive = YES` — **not** `isInteractive`, which is only the
  Swift spelling.
* `UIGlassContainerEffect` is also ObjC (`spacing` property). Not used here:
  glass is not meant to be stacked on glass, so the panel is the glass and the
  keys are ordinary content sitting on it.
* `UICornerConfiguration` is `s:5UIKit21UICornerConfigurationV` — a **Swift
  struct**, so `view.cornerConfiguration` is unreachable from Objective-C. The
  shape therefore comes from `layer.cornerRadius` + `clipsToBounds`, which is
  exactly what Apple's own "Implementing Liquid Glass" UIKit guidance shows.
* `UIButton.Configuration.glass()` / `.prominentGlass()` exist but resolve to
  Swift-overlay symbols (`s:So8UIButtonC5UIKitE…`); no ObjC factory name was
  verifiable, so the keys do not use them.

That last point is also why the panel has **no pointer/arrow**. An arrow needs a
`layer.mask`, and a mask is what stops glass shaping and lighting its own edge.
iOS 26 dropped popover arrows anyway; anchoring to the field's leading edge is
the current idiom.

### The build now requires the iOS 26 SDK

`UIGlassEffect` is referenced directly — no `NSClassFromString` probe, no
`UIBlurEffect` fallback — so:

* `IOS_MIN` is **26.0** in step3-fast, step3-gui and step4 (clang errors on an
  API newer than the deployment target, and linking against the iOS 26 SDK is
  also what makes iOS give the whole app the new design language instead of
  compatibility mode).
* Every one of those workflows starts with a **Select Xcode** step that picks
  the newest `/Applications/Xcode*.app` and **fails in under a minute** if its
  iOS SDK is older than 26, printing what is installed. Step 4 takes a `runner:`
  dispatch input (default `macos-15`) so the retry can move to `macos-26`
  without editing the file.
* The source carries a matching `#error` on
  `__IPHONE_OS_VERSION_MAX_ALLOWED < 260000`, so a local build says why rather
  than reporting an unknown class.

Expect this run to miss ccache completely: a different compiler is a different
cache key. The deps and wx prefixes still hit — static archives built with an
older minimum link into a newer one fine.

## Opening saved projects: four symptoms, two causes

Reported: the same project shows up twice, sometimes it "can't be opened",
sometimes it opens to a white Prepare viewport, sometimes there is no preview.
The device log settles the first two outright. This line is the whole story:

```
load_project filename is: /private/var/mobile/Containers/Data/Application/
A5B282A3-066F-4E5D-8E7D-497FE3E17DEB/tmp/org.orca-ios.orcaslicer.9YHLT3UZJ6-Inbox/
1204 TOKA Base.3mf and originfile is: <loadall>
```

**Cause 1 — projects were being kept in `tmp`.** `UIDocumentPickerViewController`
is created with `asCopy:YES`, so iOS hands back a copy in
`tmp/<bundle-id>-Inbox/`, and `Plater::priv::set_project_filename` records that
path in the recent-projects list. iOS empties `tmp` whenever it likes, so the
entry is dead by the next session — `MainFrame::open_recent_project` then finds
`!wxFileExists` and puts up *"The project is no longer available"*. And each
pick gets its **own** Inbox directory, so opening one project three times
records three different paths for it: three entries on the home page with the
same name.

**Cause 2 — the container UUID changes on every install.** Every absolute path
here contains it (`/Containers/Data/Application/<UUID>/…`), and the logs from
one evening carry three different UUIDs, because the app is replaced by a new
sideloaded build every few hours. Every path recorded before the last install
points into a container that no longer exists.

Patch 0382 fixes both:

* `orca_ios_stable_project_path()` moves anything that is not already under
  `$HOME/Documents/` into `Documents/Projects/` — applied in `GUI_App::load_project`
  (the Open picker), `GUI_App::import_model` (the Import picker, which is what
  the `<loadall>` line above came through) and `GUI_App::MacOpenFiles`. Same
  file picked twice now yields the same path, so the recent list de-duplicates.
  `UIFileSharingEnabled` and `LSSupportsOpeningDocumentsInPlace` are already on,
  so `Documents/Projects` is visible in the Files app.
* `orca_ios_repair_container_path()` splices the *current* container in front of
  a recorded suffix, and failing that looks for the basename in
  `Documents/{Projects,Downloads,Autosave}` — applied in `open_recent_project`
  and `get_recent_projects`, so entries written by earlier installs heal
  themselves instead of showing "File is missing".

### The third cause, for the duplicate-model case specifically

`Plater::load_project` is re-entrant on iOS. `wxFileDialog::ShowModal` in the
iPhone port drives the picker through a **nested run loop**, so everything the
app has queued keeps being delivered while the picker is up — including another
`load_project`. Upstream's `m_loading_project` guard is set *after*
`close_with_confirm()` returns, and `close_with_confirm()` is where the picker
is shown, so the entire re-entrant window sits before the flag. Two loads racing
through `reset()` and `load_files()` is what a doubled model and a half-built
scene look like. 0382 adds an iOS-only RAII guard at the very top of the
function. Deliberately *not* fixed in `filedlg.mm`: that lives in `wx-overlay`,
and touching it changes `WX_KEY` and buys a 25-minute wxWidgets rebuild.

### White viewport / missing preview: not yet proven

The plausible mechanism is that the canvas spends the load underneath a modal
picker and a tab switch, and nothing marks the scene dirty afterwards, so the
frame left on screen is the empty one from before the project existed and no
second frame is ever requested. 0382 asks for that frame
(`set_as_dirty` + `request_extra_frame` + `Refresh`) at the end of
`load_project` and logs `objects/instances/volumes/res` alongside it. If a white
viewport survives that, the counts in the log say whether the model arrived at
all — which splits "nothing was loaded" from "loaded but not drawn".

### Build note

`XCODE_TAG` is now part of the deps cache key. CMake rebuilds the whole
dependency tree when the compiler changes, and the key was only a hash of
`patches/step1`, so the entry saved by the old toolchain kept winning and every
run redid a 40-minute rebuild it was never allowed to save. Run 121 paid exactly
that. The wx key deliberately does not include it: static archives built against
an older SDK link into a newer one, and only the final link decides which SDK
the app reports.

## Auto-connect, and the camera being already on

Two complaints, one shared root: the app only did these things when the Device
tab was opened.

### Why it "only connected when I went to the Device tab"

`ios_announce_preconfigured_printer()` (patch 0348) selected the machine exactly
once, at launch — and *selecting is what dials*. The call sits a few
milliseconds after `orca_ios_request_local_network_permission()` raises the iOS
local-network prompt, so on a fresh install the only connect attempt ever made
is the one made while the prompt is still on screen. It cannot succeed. Nothing
retried it: the 5-second beacon re-announced the machine (so Orca would not age
it out) but deliberately passed `select_it = false`, because re-selecting a LAN
machine tears the session down and starts over.

The connection therefore appeared only when something *else* selected the
machine — and opening the Device tab is precisely that.

Patch 0383 makes the beacon ask whether the session is actually up, and dial
again when it is not:

* `bambu_lan_agent_is_connected()` / `bambu_lan_agent_is_connecting()` are new
  free functions in `BambuLanPrinterAgent.cpp`, backed by an atomic pointer to
  the one agent in the process. **Do not** reach for
  `MachineObject::is_connected()` here — for a LAN-mode printer it returns
  `true` unconditionally, which tells you nothing.
* A dial is skipped while a connect is in flight (they run on a worker with a
  6-second timeout) and never repeats inside 6 seconds. So after the user taps
  Allow, the session comes up within about one beacon.
* Session up/down transitions are logged once each, not every 5 seconds.

### Why the camera was never already playing

`MediaPlayCtrl::Play()` returns immediately unless `m_next_retry.IsValid()`, and
the only two things that ever make it valid are the play button and the
IP-address dialog — upstream's autoplay line is commented out on purpose:

```cpp
//#if BBL_RELEASE_TO_PUBLIC
//    m_next_retry = wxDateTime::Now();
//#endif
```

so this build never auto-started the stream at all. On top of that `Play()`
bails on `!IsShownOnScreen()`, and `on_show_hide()` calls `Stop()` on the way
out of the tab. Three iOS-only changes in 0383: set `m_next_retry` in the
constructor, drop the `IsShownOnScreen()` gate, and keep playing when the page
is hidden. `SetMachineObject()` already calls `Play()` on every status update
while the state is idle, so the stream now starts as soon as the printer is
connected and pushing status — which is what `Enable()` keys off.

Worth knowing: the stream now runs while the user is on Prepare. That is the
point (returning to the tab is instant instead of paying for a TLS handshake and
a first frame every time), but it is continuous WiFi and JPEG decoding. If
battery becomes a complaint, stop on `UIApplicationDidEnterBackground` rather
than on tab change.

## The "bad slicing" was never slicing — it was a 16-bit index in a shader

Reported as models coming out "only partly or just weird sliced", and then more
precisely: on the iPad only about one percent at the bottom appears, the rest
looks unsliced — but the estimated times are close to the PC's.

That last detail is the whole clue, and the two project files settle it. The
same model, sliced on each:

| | iPad | Windows |
|---|---|---|
| `plate_1.gcode` | 69.6 MB | 73.9 MB |
| total layers | 1000 | 1000 |
| max Z | 100.5 mm | 100.5 mm |
| prediction | 32639 s | 34183 s |
| weight | 35.84 g | 35.71 g |
| filament | 11.83 m | 11.78 m |
| nozzle | 0.2 | 0.2 |

**The slicing is fine.** The G-code is complete and within a few percent of the
desktop's on every measure. (This also kills the nozzle hypothesis recorded
earlier: the Windows profile is a 0.2 nozzle too. Do not spend more time on it.)

### What was actually wrong

`src/libvgcode/src/ShadersES.hpp`, the ES vertex shader that draws the toolpath.
It reads the entire path out of textures — segment indices from a `usampler2D`,
positions, heights/widths/angles and packed RGB24 colours from `sampler2D`s —
and it declared:

```glsl
#version 300 es
precision lowp usampler2D;
```

In GLSL ES the default precision for a sampler **in a vertex shader is lowp**,
so the three unqualified `sampler2D`s were lowp as well, and the one explicit
declaration asked for lowp too. Apple's GPUs implement lowp as 16 bits.

A 16-bit segment index wraps at 65536. This model is roughly two million
segments, so the first few percent got the right index and everything above
wrapped back into the same low range and was drawn on top of the bottom layers —
indistinguishable from "the rest was never sliced". `decode_color()` unpacks an
RGB24 integer out of a float, so the colours were wrong past 65504 by the same
mechanism.

Patch 0384 declares `highp` for float, int, `sampler2D` and `usampler2D` in both
vertex shaders that sample those textures. ES 3.0 requires highp in vertex
shaders, so it costs nothing.

**The lesson worth keeping:** when a symptom looks like bad *data*, check
whether the data is actually bad before touching the code that produces it. Two
files and `awk` were enough here, and they pointed at the opposite end of the
pipeline from where three earlier hypotheses were looking.

### "The preview makes the app struggle" — what the counters already say

Do not reach for frame caching. The `orca-ios-canvas` counters, which have been
in every log since patch 0364, already report the render rate:

```
last 5s idle +364 paint +0 render +83  mouse +65 ...
last 5s idle +101 paint +0 render +3   mouse +6  ...
```

Three renders in five seconds while the user is not touching anything, ~17/s
while dragging. The canvas is *already* demand-driven — nothing is redrawing a
static scene. So the cost is per frame, not frames per second, and the levers
are the number of instances drawn, the texture memory held, or the rebuild of
the enabled set when the layer slider moves.

Two things follow. First, until 0384 lands, roughly 97% of the segments in a
big preview were being drawn with **wrapped indices** — garbage geometry, quite
possibly enormous triangles, which is about the most expensive thing you can
hand a tile-based GPU. A slow preview and a preview showing 1% are plausibly the
same bug, so re-measure after 0384 before optimising anything.

Second, 0385 adds one line per preview load so the next round is measured rather
than guessed:

```
orca-ios-preview: vertices N, enabled segments N, textures N MB, chunks N, load N ms
```

Segment count bounds the draw, texture bytes bound the memory, elapsed time
bounds the load. Together with the render counter above, the per-frame cost
falls out of arithmetic instead of speculation.

## Run 124: the preview, measured

The instrumentation paid off immediately. One line, from a real session:

```
orca-ios-preview: vertices 2397036, layers 500, cpu 183 MB, gpu 87 MB, load 149 ms
```

2.4 **million** segments. The load is not the problem — 149 ms. The memory is
real but survivable — 270 MB. What matters is what a frame costs, and the canvas
counters from the same session show the shape of it:

```
sel 1  idle +394  render +212  mouse +222   <- Prepare, interactive: ~42 fps
sel 2  idle +129  render +109  mouse +0     <- Preview, nothing touched: ~22 fps
sel 2  idle +6    render +4    mouse +14    <- Preview, being touched: ~0.8 fps
```

Two separate facts:

1. **The preview redraws ~22 times a second with no input at all.** Something
   asks for continuous frames there — `GLCanvas3D::on_idle` renders again
   whenever `imgui_requires_extra_frame` or `wxGetApp().imgui()->requires_extra_frame()`
   is set, and the Preview's legend is an ImGui window. Drawing 2.4M instances
   22 times a second for a static scene is pure waste.
2. **A frame under interaction takes about a second.** `idle +6` in five seconds
   is the tell: the main thread is not idle, it is inside something. The
   candidate worth naming is the picking pass — it draws the scene *again* into
   an offscreen target and ends in `glReadPixels`, which blocks the CPU until
   the GPU has drained. On a 2.4M instance scene that is exactly how a slow
   frame becomes a frozen one.

Patch 0386 times all three: milliseconds inside `render()` per five seconds, how
many of those were the picking pass, and the worst single frame. The next log
says which of the two facts to fix, and the fix follows from the number rather
than from a guess.

### The crash

Signal 11 with no frame of ours on the stack:

```
objc_retainAutoreleaseReturnValue + 52
UIKitCore ...
_CFXNotificationPost + 680
_UIScenePerformActionsWithLifecycleActionMask
_UISceneSettingsDiffActionPerformChangesWithTransitionContextAndCompletion
```

A notification posted during a UIScene settings change, reaching an object that
has been freed. The strongest candidate is the numeric keypad, and the mechanism
is specific: a subview is retained by its superview, but the pad is otherwise
owned only by its text field through an associated object. `-hide` used to leave
the pad in the window for the length of a 0.12 s fade. A field destroyed inside
that window — and Orca rebuilds sidebar fields constantly — drops the only
ownership while the window still holds a reference, so the pad outlives its own
field: still registered for notifications, still holding a freed pointer. The
next scene-lifecycle notification messages it.

0381 now removes the pad from the window synchronously. Not proven to be the
crash, but it is a genuine use-after-free either way, and a fade is not worth
one.

### The duplicate in the recent list

Not yet explained. The evaluateJavaScript log truncates the recent-projects JSON
at 100 characters, so the list itself has never been visible. 0386 logs each
entry's full path as `orca-ios-recent: [i] <path>`, which separates the two
possible causes: two history entries for one file, or two files with the same
name in different directories.

### The duplicate in the recent list — answered by the user, not by the log

"The duplicate was once the downloaded model from MakerWorld and once the
already imported saved. Only the saved imported should be in the menu."

So it was never two history entries for one file, nor two files with the same
name by accident: it is the **staging copy** and the **project**. Opening a
MakerWorld download loads `Documents/Downloads/<name>.3mf`, and
`Plater::priv::set_project_filename` puts whatever was loaded on the home page.
Saving then puts the real project there too. Same model, twice, and the entry
the user never asked for is indistinguishable by name.

Patch 0387: `MainFrame::add_to_recent_projects` skips anything under
`$HOME/Documents/Downloads/` — the directory `init_download_path()` owns, which
nothing else writes to. `get_recent_projects` filters the same way, so entries
recorded by earlier builds disappear from the page immediately rather than
waiting for the list to turn over.

Note this does not strand an unsaved download: the autosave entry (patch 0369)
is still listed, and the file itself is still in Documents/Downloads, visible in
the Files app.

## The preview frame, phase by phase

Run 125's timing, from a real session on the 2.4M segment preview:

```
sel 1 (Prepare)  render +219  render_ms 2794  pick_ms 0  worst_ms 26
sel 2 (Preview)  render +109  render_ms 4471  pick_ms 0  worst_ms 1268   (mouse +0)
sel 2 (Preview)  render +4    render_ms 4692  pick_ms 0  worst_ms 1181   (mouse +17)
```

Three facts, and one dead hypothesis:

* **The picking pass is not involved.** `pick_ms 0` throughout — the preview
  canvas has picking disabled, so `_picking_pass()` returns immediately. That
  was my hypothesis and it was wrong; the measurement cost one round and saved
  building the wrong fix.
* **Prepare is fine**: 219 frames in five seconds, 12.7 ms each.
* **The preview costs ~41 ms per frame at rest** (109 frames, 4471 ms) and
  **~1173 ms per frame while being touched** (4 frames, 4692 ms). Same scene,
  same camera, 40x apart. And in both cases the main thread is inside
  `render()` about 90% of the wall clock.

An FPS cap cannot help: 41 ms is already 24 fps, below any cap worth setting.

Two candidates remain for the 40x, and they need opposite fixes:

1. **The toolpath draw itself**, made worse under touch by GPU backpressure —
   `presentRenderbuffer` blocks once the GPU is a frame or two behind, so a
   scene that only just keeps up at rest collapses when anything is added.
   Fix: submit less.
2. **The enabled-segment rebuild.** Dragging a layer or moves slider makes
   libvgcode rebuild `enabled_segments`/`enabled_options` from scratch and
   re-upload them — tens of megabytes per drag step at this size. That work
   happens inside `_render_gcode()`, so it lands inside `render()` exactly as
   observed. Fix: coalesce the rebuild across a drag.

Patch 0388 times `_render_gcode()` (which covers both the viewer draw and the
slider handling) and `SwapBuffers()` separately. `render_ms - gcode_ms -
swap_ms` is then the bed, the objects, ImGui and the overlays, so nothing is
left unaccounted for. **This is the last measurement round**; the two remaining
candidates have opposite fixes and the numbers choose between them.

## Preview defaults on iOS

Two presentation defaults, asked for directly:

* **View type opens on Summary**, not feature type. On a tablet the first thing
  wanted from a finished slice is what it will cost — time, filament, weight —
  and the feature-type colouring is something to switch to deliberately.
  Changed in **two** places: `GCodeViewer` sets it at the first slice, and
  `load()` re-applies a default whenever the used-extruder count changes.
  Patching only the first leaves every re-slice putting feature type back.
* **The G-code text window is hidden.** It covers a quarter of the preview and
  its content is the kind of thing read next to a keyboard.

The second needed a one-time migration, and the reason is worth remembering:
`AppConfig::set_defaults()` only fills in keys that are **missing**, and it runs
*after* an existing config has been loaded. Changing the default therefore does
nothing on any device the app has already run on — which is all of them. So the
new value is applied once, recorded under `ios_preview_defaults`, and a user who
turns the window back on afterwards keeps it on. Any future default flip needs
the same treatment, with a new key.

Neither of these is a performance fix. Summary still draws every segment; the
view type only decides the colouring.

## The preview: found it

The partition made it unambiguous:

```
sel 2  render +6  render_ms 4672  gcode_ms 4657  swap_ms 0  worst_ms 793
sel 2  render +5  render_ms 3875  gcode_ms 3865  swap_ms 1  worst_ms 812
```

`gcode_ms` **is** the frame, and `swap_ms` is nothing. That second number is the
important one: if the GPU were the bottleneck, `presentRenderbuffer` would block
and `swap_ms` would be large. It is zero, so the GPU takes each frame instantly
and the ~790 ms is **CPU work inside `_render_gcode()`**. It is not the drawing.

What that CPU work is: handing a new range to the viewer makes libvgcode walk
every vertex to rebuild its enabled-segment, colour and height/width/angle
arrays and re-upload them. At 1.6M vertices that is ~775 ms, and it was being
done **once per drag step**, from inside the render.

Throttling cannot fix a 775 ms unit of work — even one per second leaves the app
unusable — so patch 0390 makes the rebuild wait until the slider **stops**:
`orca_ios_slider_settled()` reports settled once the range has held still for
250 ms, and until then the slider is left dirty and an extra frame is requested.
During a drag the slider still moves and still draws, at the ~39 ms per frame
the scene costs on its own; the toolpaths catch up a quarter second after the
user lets go. The shells still follow immediately — that range is a handful of
volumes, not a toolpath.

Two independent slots, because the layer slider and the moves slider settle
separately.

**What this does not fix:** the preview still costs ~39 ms a frame at rest
(~25 fps) because the scene really is 1.6-2.4M segments, and it still redraws
continuously while ImGui asks for frames. Both are worth attention if the
preview still feels heavy after this, and both are now measurable with the same
counter line.

## Two-finger pan never reached the app

Orca's `GLCanvas3D::on_gesture()` has handled `wxEVT_GESTURE_PAN` correctly the
whole time — it converts the delta through `_mouse_to_3d` and moves the camera
target. The event simply never arrived.

**UIKit arbitrates.** Without a delegate saying otherwise, only *one* recognizer
on a view may recognize at a time; the rest are made to fail. Pinch and rotation
both begin the instant a second finger lands, so a two-finger drag was always
claimed by one of them and the pan recognizer never sent anything. None of the
recognizers installed by `EnableTouchEvents` had a delegate.

Patch **step2/0228** adds one shared `wxOSXGestureDelegate` returning YES from
`gestureRecognizer:shouldRecognizeSimultaneouslyWithGestureRecognizer:`, and
sets it on the pan, pinch and rotate recognizers. A 3D viewport wants all three
at once — drag to pan, spread to zoom, twist to rotate, in one motion — which is
what every iPad app with a camera in it does.

### And the trackpad

Two-finger scroll from a trackpad is not a touch at all: it reaches an app only
through a pan recognizer that opts in via `allowedScrollTypesMask`, and it
carries **no touches**, which is why the existing two-finger pan recognizer
(`minimumNumberOfTouches = 2`) could never see it. It was going to the separate
scroll recognizer, which turns it into a mouse wheel — a zoom.

Split by scroll type, which is exactly what UIKit provides the distinction for:

* the wheel recognizer takes `UIScrollTypeMaskDiscrete` — a real mouse wheel,
  still zooming, unchanged;
* a new trackpad recognizer takes `UIScrollTypeMaskContinuous` and emits a pan.

A separate recognizer rather than reusing the touch pan, because that one's
two-touch minimum cannot be satisfied by an indirect scroll. Its handler ignores
anything carrying touches, and `cancelsTouchesInView = NO` keeps it out of the
pointer's way.

**Cost:** this is `patches/step2`, so `WX_KEY` changes and wxWidgets rebuilds
once (~25 min). Nothing else in the tree is affected.

## Where the preview time actually goes — the mechanism

`ViewerImpl::render()` is not just a draw. Every frame it runs three
**O(vertices)** rebuilds behind dirty flags:

```cpp
if (m_settings.update_view_full_range)  update_view_full_range();
if (m_settings.update_enabled_entities) update_enabled_entities();
if (m_settings.update_colors)           update_colors();
render_segments(...);
```

Each walks all 2.4M vertices, builds multi-megabyte vectors and re-uploads
textures — **inside the frame**. So anything that sets a flag does not cost
itself: it makes the *next frame* cost ~1.2 seconds. That is the signature the
numbers showed all along — `gcode_ms ≈ render_ms`, `swap_ms ≈ 0` — a frame that
is not slow to draw but is being rebuilt.

At rest the flags are clear and a preview frame is ~35 ms (28 fps). Under touch
something sets a flag every frame.

Patch 0391 times the three calls separately (`vg_range_ms`, `vg_enabled_ms`,
`vg_colors_ms`), which names the flag. The atomics live in libvgcode and are
read by MainFrame across the library boundary — both end up in the same binary,
so the symbols resolve; libvgcode still gets no boost dependency, which is what
broke run 123.

### Reading the run-128 log

Worth recording so the next person does not misread it: the session at 17:35
local (15:35 UTC) was **run 128**, which does *not* contain patch 0390. Run 130
finished at 15:34 UTC, one minute earlier — not enough time to publish, download
and sideload. So that log says nothing about whether settling the slider helped.

### What "buttery smooth" needs

1. **No rebuild during interaction.** 0390 (slider settle) is the first half;
   0391 will say whether anything else sets a flag per frame.
2. **The 35 ms baseline.** Even with zero rebuilds, 2.4M segments cost ~35 ms a
   frame — 28 fps, not 60. `swap_ms` is ~0, so this is CPU-side, not the GPU
   drawing: worth splitting `_render_gcode()` into shells / toolpaths / legend /
   sliders once the rebuilds are gone, because 35 ms of CPU for one instanced
   draw call is far too much and something in there is still iterating.

## The rebuild is gone, and the frame is still 2.4 seconds

Run 132's log, the 2.4M segment preview, at rest:

```
sel 2 (plater); render +2  render_ms 4838  gcode_ms 4815  swap_ms 9
                vg_range_ms 0  vg_enabled_ms 0  vg_colors_ms 0
```

Two frames in five seconds — **2407 ms each, 0.4 fps**. Read it line by line,
because between them these numbers close the question that 0388 and 0391 were
built to answer:

* **`vg_* = 0`.** None of the three `O(vertices)` rebuilds ran. 0390 worked, and
  the "something sets a flag every frame" hypothesis from the last round is
  dead. It was a real cause and it is fixed; it was not this one.
* **`swap_ms` 9 for two frames.** Nothing is waiting on the present.
* **`render_ms - gcode_ms` = 23 ms for two frames.** The bed, the objects, ImGui
  and the overlays cost ~11 ms a frame. Not the problem — but note that 11 ms is
  already a 90 fps ceiling on its own, so it does not get to stay either.
* **`gcode_ms` is the frame.** All 2.4 seconds of it are inside
  `GCodeViewer::render()`, and none of it is a rebuild.

The preview size line says what is being drawn:

```
orca-ios-preview: vertices 2401216, layers 500, cpu 183 MB, gpu 87 MB, load 254 ms
```

`render_segments()` on the ES path issues **one**
`glDrawArraysInstanced(GL_TRIANGLES, 0, 24, enabled_segments)`. At 2.4M enabled
that is **57.6 million vertices per frame**, each doing four dependent
`texelFetch`es out of RGBA32F textures. `render_options()` then draws the seams,
and `Settings.hpp` ships `true, // Seams` — at **96 vertices per seam**, two
48-vertex cones, which is four times what a segment costs. A 500 layer model has
a seam per perimeter loop per layer.

That is past what a tile-based GPU accepts in one pass: the parameter buffer
overruns, the driver splits the frame into partial renders of a 4 megapixel
target, and the block lands **inside the draw call** while it waits for memory
to drain. Which is exactly the signature — `gcode_ms` enormous, `swap_ms` zero.
A frame that is slow to draw, at last, rather than one that is being rebuilt.

For comparison, Prepare on the same drawable: 201 frames in five seconds, 16 ms
each. Same canvas, same device, a scene of a few hundred thousand triangles.

### Why this one cannot be tuned

120 Hz is an 8333 us frame. 57.6M vertices needs ≥190 ms even at a generous
300M vertices/s. There is no version of "draw the same thing faster" that closes
a gap of that size. The scene has to get smaller while it is moving, which is
what every CAD viewport does and what the preview has never done.

Two other things had to be true first, and only one of them was:

* **The app is capped at 60 fps and always has been.** `Info.plist` had no
  `CADisableMinimumFrameDuration`. Without that key UIKit holds every app to a
  16.67 ms minimum frame duration on a ProMotion panel, so no amount of cheaper
  frames could ever have produced more than 60 of them. Added.
* **Low Power Mode pins ProMotion to 60 Hz**, at the system level, for every
  app. 120 fps in battery saver is not achievable on iOS and never will be. The
  honest target there is 60 held solidly, which the same work delivers.

### Level of detail, applied by the draw call — SUPERSEDED, see "Draw the same picture, cheaper" below

Patch **0392**. `gl_InstanceID` indexes the enabled-segments texture directly,
so instance *i* can just as easily read entry *i × stride*: adding an
`instance_stride` uniform to the two ES vertex shaders and drawing `count /
stride` instances samples the whole model uniformly at any density. One uniform.
No new memory, no new buffer, and above all **no rebuild** — every other way of
making this scene cheaper (narrowing the range, hiding roles, hiding options)
goes through `update_enabled_entities()`, the `O(vertices)` walk that made
dragging unusable in the first place. This one changes per frame for free.

The uniform is read through `max(x, 1)` in the shader, so one that somehow never
got set means full detail rather than stride 0 — which would put every instance
on segment 0 and draw the model as a single smear.

Patch **0393** turns seams off by default on iOS. They are the one option shown
by default, they cost four times a segment each, and they are decoration on a
tablet. The legend's eye icon turns them back on.

### One clock, and a controller that measures instead of guessing

Patch **0394** generalises 0390's slider-only `settled` into a single
interaction clock. Orbiting, panning, pinching, zooming, scrubbing either
slider — all of them make the same frame expensive and all of them want the same
answer. Anything that moves the view stamps the clock; 250 ms of quiet is
"stopped". The slider stamp lives inside `orca_ios_slider_settled()` because
that is the only place that knows a slider is mid-drag: the drag is handled
inside ImGui and never reaches an event handler.

Patch **0395** decides how much detail to draw, and it **measures rather than
chooses**. Nothing about the cost of this draw is predictable — it depends on
the model, the device, how much of the screen the scene covers, and it is badly
non-linear because of the partial renders. So each frame reports what it drew
and how long it took, and:

* a frame that fitted its target while leaving detail undrawn doubles its budget
  — reaching any level in a few frames, overshooting by at most one frame;
* a frame that overran gets no averaging: its own cost per instance says how
  many would have fitted, and that is the budget, applied at once.

The climb stops when the stride reaches 1 and everything is drawn. The stopping
test is against the **enabled count**, not against the budget: a stride is a
whole number, so at budget 100000 and 2401216 enabled the stride is 25 and the
frame draws 96049 — forever just under its own budget, never asking for more.
That bug was written, and caught by the arithmetic before it was built.

Both states are budgeted, at targets twelve times apart — 4 ms moving, 50 ms
settled. **Neither is ever unbounded**, and that is deliberate: the main thread
sits inside the draw call for its whole duration, so a 2.4 second still frame is
2.4 seconds in which the first touch of the next drag is not being read. A
still frame is allowed to be heavy. It is not allowed to be deaf.

Against a cost model calibrated to the measured 2.4 s, the controller settles at
stride 10 / 240k instances / 3.6 ms while moving and stride 4 / 600k / 33 ms at
rest; models of ordinary size reach stride 1 and are untouched. Those numbers
are a simulation of a curve we have not measured yet — the point is not the
numbers, it is that the loop finds whatever the real curve turns out to be.

### What the next log will say

Patch **0396** splits `gcode_ms` into the six phases it is made of and reports
the detail the controller chose:

```
shells_ms  paths_ms  legend_ms  seq_ms  slider_ms  lod_stride  lod_budget
```

`paths_ms` is the draw this whole round is about, and `lod_stride` says how much
was skipped to get it. The five sum to `gcode_ms`, so whatever is missing is the
slider handling in `_render_gcode()` itself, and nothing is left unaccounted for
a third time.

Read it in this order:

1. **`paths_ms` while dragging.** Should be ~4 ms with `lod_stride` above 1. If
   it is not, the controller is not converging and the numbers say which way.
2. **`legend_ms` + `seq_ms` + `slider_ms`.** This is the 11 ms of ImGui that
   `render_ms - gcode_ms` could not see into. At 120 Hz it has 4 ms to live in.
3. **`shells_ms`.** Transparent object meshes, drawn every preview frame.

### Still on the shelf, deliberately

**Halving the drawable while moving.** The canvas is 2070x1959 — 4 megapixels at
`contentScaleFactor` 2 — on a panel that is 2752x2064, so we already draw ~1.35x
more pixels than the display can show (`scale 2`, `nativeScale 1.72`). Dropping
the GL view to scale 1 during interaction quarters fragment and tile-store cost.
It needs a `wxGLCanvas` setter, so it is a `patches/step2` change and a 25 minute
wx rebuild. Held until the phase numbers say whether it is needed — if
`paths_ms` lands on target and the ImGui phases are what is left, it is the
wrong lever.

**Driving frames from a CADisplayLink.** Rendering is paced by wxIdle's
`RequestMore()` loop, which gave Prepare 40 fps out of 16 ms frames — 62 fps
worth of work delivered at 40. Once frames are cheap that pacing becomes the
ceiling. Also step2, also held: it is worth nothing until the frames themselves
fit, and it is the riskiest change on the list.

## Run 134: smooth, and unreadable

It worked and it looked terrible. Two things were wrong, and the screenshot
named both.

### Widening the survivors makes spikes, not thicker lines

0392 shipped a `width_scale` uniform that multiplied each surviving segment's
height and width by `sqrt(stride)`, on the theory that a decimated toolpath
should keep its visual mass rather than thin out. What came back was a mass of
teal shards with nothing recognisable as a toolpath in it.

The reason is in the segment template. `POINTY_CAPS` extends the end caps
*along* the line by `half_width`:

```glsl
pos += line_dir_sign * line_dir * half_width;
```

So width is not just the cross section — it sets the cap length too. At stride
25 the scale factor is 5, and every segment became a spike five times its own
width at each end, as long as it was wide. Not thicker lines: diamonds.

Removed, and not replaced. A decimated toolpath at **true width** reads as the
same object drawn lighter, which is exactly what it is, and "I can see the
lines" is the whole point of the view. Sparse and correct beats dense and wrong.

### A still preview is not allowed to be an approximation

The other half was mine to have known better. 0395 budgeted the settled frame at
50 ms, reasoning that a still frame must stay answerable because the main thread
sits inside the draw call for its whole duration — so a 2.4 second frame is 2.4
seconds in which the touch that ends it is not being read.

That reasoning is still true. It is also not a licence to show a different
picture. The preview is the artefact being judged; it has to be the same image
the desktop draws, and a decimated one is not that image at any budget. Detail
is what gets traded away to protect a **frame rate**, and a still image has no
frame rate to protect.

So: budgeted while moving, **full detail the moment it stops**, always.

One guard came with it. The controller must not learn from the settled frame:
that one is drawn in the regime where the driver splits the render, and its cost
per instance is not the cost per instance of a frame that fits. Feeding it back
would collapse the budget to the floor the instant the next drag began, making
the first frame of every drag the worst-looking one. Only a moving frame that
followed another moving frame is measured (`prev_moving`), which the arithmetic
test checks explicitly.

### What this costs, and the number that decides it

Full detail at rest costs whatever a full-detail frame costs — on the simulated
curve, ~2.4 s. Drawn **once** that is a pause after a gesture. Drawn every idle
it is an app that is never listening.

Which of those it is comes down to one question that has never been answered:
**what keeps `m_dirty` true on a still preview?** `on_idle` sets it from eight
sources, and the run-132 log showed the canvas rendering continuously at rest,
so one of them reports a change on every idle. From outside they are
indistinguishable — a notification animating, an ImGui window asking for another
frame and a toolbar recomputing its state all look the same.

The run-134 screenshot has a notification sitting on it ("Processing model
'Exported3DModel.3mf' with more than 1M triangles could be slow"), which makes
`NotificationManager::update_notifications` the first suspect. Suspect, not
answer. So each source is now counted separately and named in the report:

```
dirty tb N notif N imgui N m3d N gizmo N tex N elsewhere N
```

On a still preview that nothing has changed, every one of those should be zero
and the render count should be zero with them. Whichever is not zero is the
thing to fix, and fixing it is what turns "full detail at rest" from a pause
into a single frame — after which the still preview costs nothing at all and the
next touch is answered immediately.

`elsewhere` catches `m_dirty` being true with none of the eight accounting for
it, which would mean something called `set_as_dirty()` or `request_extra_frame()`
outside the handler — patch 0390's slider path does exactly that, so it is worth
being able to see.

## Draw the same picture, cheaper

Two rounds were spent drawing fewer segments and both looked wrong, for the same
reason each time. The instruction that closed it is the right one: *make the
normal view insanely more efficient*. A preview that is not the picture the
desktop draws is not a preview, so the draw does not get smaller — it gets
cheaper.

Decimation is gone from the default path. `set_detail_budget` stays in
libvgcode, set to zero, because it is how the measurement is expressed and
because a ten-million-segment model will want it one day. Seams are shown again:
the desktop shows them, so the tablet shows them.

### Where the cost actually is, counted rather than guessed

One `glDrawArraysInstanced(GL_TRIANGLES, 0, 24, 2401216)`. Per **vertex**, the
segments shader:

* unfolds a linear index into a texel coordinate **six times** — the segment
  index, both endpoint positions, two height/width/angle triples and the colour;
* each unfold was `ivec2(id % w, id / w)` with `w` read back from
  `textureSize()`;
* then builds the view-aligned box and lights it.

Three structural costs came out of that, and none of them is the drawing:

**1. 24 vertex shader invocations per segment, for 8 corners.** The template is
a triangle list naming eight corners 24 times. 57.6 million invocations a frame,
and 57.6 million post-transform vertices for the tiler to bin — which is several
times what an Apple GPU takes in one pass, so the parameter buffer overruns and
the driver splits the frame into partial renders of a 4 megapixel target. That
is where seconds-per-frame comes from, and it is why the block lands *inside*
the draw call with `swap_ms` at zero.

Patch **0393** draws both templates indexed. Eight distinct indices per
instance, so the post-transform cache shades each corner once: **8 invocations
instead of 24**, and one third of the data to bin. Same triangles, same winding,
same pixels. The option marker gets the same treatment — a cone of `3 *
resolution` listed vertices has only `resolution + 1` distinct ones, so 48
becomes 17, and an option is two cones against a segment's one box.

**2. Twelve integer divisions per vertex.** Apple's GPUs have no integer divide
instruction; each `%` and each `/` is a synthesised sequence. Six of each, per
vertex, 57.6 million times a frame — plausibly more than everything else the
shader does put together.

Patch **0392** makes every texture width a power of two and hands the shader
`log2(width)` as a uniform, so the unfold is `ivec2(id & mask, id >> shift)`.
Two instructions instead of two dozen. The widths were *already* powers of two
for any model big enough to matter (the width saturates at
`GL_MAX_TEXTURE_SIZE`); the shader simply had no way to know it.

Rounding the width up meant the upload's last-row arithmetic had to be made
honest as well. It computed the tail as `remaining % w`, which is **zero when
the data fills its rows exactly** — and then uploaded nothing and recorded a
count of zero. That was survivable when `w` was the data's own length; with
power-of-two widths it stops being a remote case. All five upload paths now
count complete rows and tail separately.

**3. The varying.** `out vec3 color` at highp is 12 bytes of tiler traffic per
vertex on top of `gl_Position`. The colour arrives as three 8-bit channels and
leaves through an 8-bit framebuffer, so `mediump` carries it exactly and halves
that.

Two smaller ones in the same patch: the 16-entry sign table moved from inside
`main()` to file scope, because a dynamically indexed const array declared in a
function can land in per-invocation scratch memory rather than in constant
memory once; and `textureSize()` is gone entirely, six calls per vertex with it.

### What this is expected to be worth, and how to check

Vertex invocations: **57.6M → 19.2M**. Per-invocation work: six `textureSize`
and twelve integer divides removed. Tiler traffic: a third of the vertices, each
with half the varying.

That is a large multiple, not a small one, and the step change — if there is one
— comes from dropping under the parameter-buffer cliff rather than from the
arithmetic. Below that cliff the frame should scale with the work again.

`paths_ms` with `lod_stride 1` is the number. It is now measuring the same
picture the desktop draws, so it is comparable to the 2.4 seconds run 132
measured, and to nothing that happened in between.

### If it is not enough

The remaining levers, in the order they are worth trying:

1. **Stop redrawing a still preview.** The `dirty` counters added in the last
   round name what keeps `m_dirty` true; whatever it is, a still preview should
   render once. This is worth doing whatever `paths_ms` says.
2. **Halve the drawable while moving.** 2070x1959 is 4 megapixels on a panel of
   2752x2064 — already more pixels than the display has. `patches/step2`, so a
   wx rebuild.
3. **A CADisplayLink instead of wxIdle pacing.** Prepare gets 40 fps out of
   16 ms frames, so the pacing becomes the ceiling once frames are cheap.

None of the three changes a single pixel of the toolpath, which is now the
constraint every option has to satisfy.

## Why there are 2.4 million segments, and what to do about it

The notification sitting on the run-134 screenshot is the whole explanation:

> Processing model 'Exported3DModel.3mf' with more than 1M triangles could be
> slow. It is highly recommended to simplify the model.

A mesh that dense produces a perimeter polygon with a vertex wherever a triangle
edge crosses the layer plane. So the toolpath is carved into hundreds of
thousands of micro-moves that are **already straight** — adjacent triangles on a
smooth surface differ in direction by a fraction of a degree. The slicer has no
reason to simplify them, and the viewer faithfully draws every one.

2.4 million segments is not 2.4 million *shapes*. It is a much smaller number of
lines, over-sampled by the mesh.

### Merging is not a level of detail

Twenty collinear segments with the same width, colour and speed draw exactly the
same pixels as one segment spanning the same distance. The shader builds a box
from two endpoints and lights it; the merged segment's endpoints, widths, angles
and colours are the originals. The intermediate vertices contribute nothing at
all except vertex shader invocations.

So `coalesce_collinear_runs()` turns the enabled list into (first, last) pairs,
extending a run while **all** of these hold:

* the next entry is the consecutive vertex — never merge across a gap in the
  enabled set, so a hidden role or a layer boundary always cuts the run;
* both of the new segment's vertices match the run's first in everything that
  can change how it is drawn — role, move type, extruder, colour id, layer,
  width, height, and every scalar any view type colours by;
* the vertex the chord would newly reach is within `COLLINEAR_EPS_MM` (0.01 mm)
  of the run's anchor line;
* that vertex is **further along** the anchor line than the chord already
  reaches.

The index texture is `RG32UI` instead of `R32UI`, so the shader gets both
endpoints from the fetch it was already making. `id_b = id_a + 1` is gone.

0.01 mm is a fortieth of a 0.4 mm extrusion — smaller than the line it is drawn
with, and far under one screen pixel at any zoom that fits the bed. This is not
the same kind of change as decimation: decimation removed lines, this removes
points that were never visible.

### Two bugs the tests caught before the build did

Both were in the first cut and both would have looked like "it works":

**The collinearity test was on the wrong vertex.** The run's anchor line is
defined by its first two vertices, so the first vertex a run tries to swallow
lies exactly *on* that line at distance zero — and every run merged at least two
segments regardless of geometry. A 90-degree zig-zag merged. The test has to be
on the vertex the chord would newly **reach**, not the one it would swallow.

**A path that runs out and comes straight back has every vertex on the anchor
line.** Distance zero for all of them, so it merged into a chord from the start
to wherever it ended up — deleting the excursion. Hence the monotonic-progress
test: the new endpoint must be further along the line than the chord already
reaches. The turn now ends the run.

The harness (`mergecheck.cpp` in the scratch tree, reproduced by the numbers
below) covers both, plus the cases that must **not** merge:

```
gentle arc     : 200000 -> 3125 segments (64.0x), worst deviation 0.00000 mm
sharp zig-zag  : 200000 -> 200000 segments (must be 1.0x)
colour every 10: 200000 -> 40000 segments
dead straight  : 200000 -> 3125 segments (64.0x)
gap in enabled : 6 -> 2 segments, pairs (0,3) (50,53)
out and back   : 40 -> 2 segments, first pair (0,20)
```

64x is the `MAX_RUN_SEGMENTS` cap, not the algorithm giving up. What a real
toolpath gives is unknown until the device says so, which is why the report now
carries `segs <raw>-><drawn>`.

### The whole stack, and what each part is worth

| | vertices per frame at 2.4M segments |
|---|---|
| before | 57 600 000 |
| indexed templates (0393), 24 corners named → 8 shaded | 19 200 000 |
| collinear merge (0392), at a merge ratio of *n* | 19 200 000 / *n* |

and per invocation, six `textureSize` calls and twelve integer divides are gone,
and the varying is half the size.

If the merge ratio on this model is even 4x, the frame submits 4.8M vertices
instead of 57.6M — a twelfth of the geometry, each vertex cheaper. That is the
first time any of this has been in the right order of magnitude for 120 Hz.

`segs` in the report is the number that says whether the premise held. If it
comes back near 1x, the model's toolpath is genuinely not collinear and the
answer is elsewhere — but then the mesh's triangle count is a red herring too,
and that would itself be worth knowing.

## The iOS-native UI pass (patches 0397-0400)

The port has reached the point where the remaining problem is not "does it
run" but "does it belong on an iPad". Orca's chrome is all custom-drawn — a
wxControl for the tab strip, a GL-rendered toolbar inside the canvas — which is
exactly what made the port tractable and exactly what makes it feel like a
desktop application running in a phone-shaped window. These patches replace the
chrome with the system's own controls, one element at a time.

The rule for all of them: **hide, do not delete.** Every wx page, every panel
and every callback stays built and stays wired. A native control is put in
front of it and drives it. Bringing something back is deleting a rule or a
line, never re-implementing it.

### 0397 — the tab bar

The Home / Prepare / Preview / Device / Project strip is `ButtonsListCtrl`
(Notebook.hpp), a wxControl painting bitmaps into a band across the top. It is
hidden on iOS and a real `UITabBarController` drives the notebook instead,
which on iPadOS renders as the floating Liquid Glass capsule at the top of the
window and brings pointer effects, keyboard shortcuts, Dynamic Type and
VoiceOver with it.

**The controller has to own the window.** wx gives a top level window a
`UIWindow` whose `rootViewController` is a `wxUIContentViewController` holding
every wx view (`src/osx/iphone/nonownedwnd.mm`). A tab bar is not a view you can
add on top — the floating bar, its glass and its sidebar only happen when
`UITabBarController` is the controller running the window. So it takes the
window and wx's controller is re-parented under it, full bleed, below the bar.

Two consequences worth knowing before touching this code:

- **Adopt wx's controller before taking the window.** Assigning
  `rootViewController` releases the one that was there, and the window holds the
  only reference to it. `addChildViewController:` first, then the assignment.
  The other order deallocates the controller that owns every wx view in the
  application, under a live wxWindow tree.
- **The tabs show nothing.** wx's view is behind all of them, not inside any
  one, so each tab's view controller is a transparent `OrcaPassthroughView`
  whose `-hitTest:` returns nil for itself. Touches fall straight through to
  the wx view. Selecting a tab moves nothing in UIKit; it calls
  `SetSelection` and wx swaps its own pages exactly as before.

Selection is kept in step in both directions without a flag: a tab reports in
from `-viewWillAppear:`, and ignores itself when the page it stands for is
already the notebook's selection — which is true precisely when we were the
ones who asked for it. Correct even though the callback lands a run loop turn
after the request.

Sync points are all in Notebook.hpp: both `InsertPage` overloads,
`DoRemovePage`, `ChangeSelection`, and a `wxEVT_BOOKCTRL_PAGE_CHANGED` binding
in `Create()` that catches every ordinary selection change. Deliberately
nothing inside `SetSelection` — 0372 is already editing that method, and the
binding covers it.

**Project and Calibration have no tab for now.** Held back by
`orca_ios_hide_tab()` from `init_tabpanel`, by page identity rather than by
label: the labels are `_L()` and "Project" is only called Project in English.
The pages are still built, still paged, still reachable. Delete a line in
`init_tabpanel` to give one its tab back.

**iOS 26 carries all of this, and 27 would add nothing.** Worth stating
plainly, because it is easy to assume otherwise from the design language: iOS
27's UIKit is a very small release, and nothing either patch draws arrived in
it. The floating glass capsule is simply what iPadOS 26 already makes of a
`UITabBarController` with `UITab` items (iOS 18), and `UIGlassEffect` is 26 —
the guard patch 0381 installs. The only 27 symbols anywhere near this work are
`performBatchUpdates:`, `prominentTabIdentifier` and the sidebar placement
pair; none of them change what appears on screen. So `IOS_MIN` and the "Select
Xcode" gate stay at 26, which also keeps the ccache rather than paying an hour
of rebuild for an identical result.

### 0398 — the cloud accounts on the home page

`resources/web/homepage/index.html` gains one stylesheet,
`orca-overlay/resources/web/homepage/css/orca-ios.css`, which hides the Orca
Cloud section, the Bambu Cloud section and the OrcaCloud shortcut. One block per
hidden thing; delete a block to bring it back. `!important` throughout because
`home.js` sets `display` inline as the sign-in state changes.

Losing the Bambu section takes the "network plugin not detected — click here to
install it" notice with it, which is worth having gone on its own account:
there is no network plugin on iOS and there cannot be one. Printing over the
LAN does not go through that section (see `BambuLan*`) and is unaffected.

### 0399 — the plater toolbar

Orca draws its toolbar *inside the GL canvas*: `GLToolbar` composes the icons
into a texture atlas and renders them as quads across the top of the 3D view,
and the gizmo bar beside it does the same out of `GLGizmosManager`. Neither is a
control — no view, no hit target, no accessibility — and at iPad scale the row
reads as a line of dots along the top edge.

0399 puts a column of real `UIButton`s on a `UIGlassEffect` panel down the
trailing edge of the canvas instead. **The icons are the same files**: the
button images are rasterised with nanosvg straight out of `resources/images/*.svg`,
named by the C++ side, so the light and dark variants keep following Orca's own
theme. The actions are the same calls, too — a tap ends in
`GLToolbarItem::do_left_action()` or `GLGizmosManager::open_gizmo()`, which is
exactly where a click on the GL toolbar ended.

**Collapsed by default, open on the chevron.** The full set is 25 items, a
column taller than an iPad. Collapsed shows the nine used while laying out a
plate: add, add plate, auto orient, arrange, variable layer height, and the
move / rotate / scale / lay on face gizmos. The set is a flag per item in
`GLCanvas3D::orca_ios_sync_toolbar()` — change it there and nowhere else.

Four things worth knowing:

- **The GL bars are un-drawn, not disabled.** `_render_main_toolbar()` returns
  early rather than `m_main_toolbar.set_enabled(false)`, because `is_enabled()`
  means far more than "is it drawn" — the same flag decides whether layer
  editing is initialised, whether the gcode viewer is built, and which bodies
  the scene renders. Same for the gizmos: `_render_gizmos_overlay()` returns
  early, where `m_gizmos.set_enabled(false)` would have taken the gizmos'
  handles out of the scene along with their icon row.
- **And their hit rectangles go with them.** An undrawn toolbar that still
  hit-tests is an invisible strip along the top of the canvas eating every drag
  that starts near it, so `m_main_toolbar.on_mouse` is skipped and
  `GLGizmosManager::gizmos_toolbar_on_mouse` returns false. Only the bar — the
  gizmos' own `on_mouse` is untouched.
- **nanosvg writes unpremultiplied RGBA**, which is why the icons go through
  `CGImageCreate` with `kCGImageAlphaLast` and a data provider rather than a
  `CGBitmapContext`: a bitmap context cannot hold unpremultiplied alpha, and
  asking it to darkens every antialiased edge.
- **Rasterised icons are cached** by name and screen scale. The sync runs from
  `on_idle`, right after `m_main_toolbar.update_items_state()`, so it runs
  constantly; it pushes a description, and the native side does nothing at all
  when that description has not changed.

`GLToolbar` gains one accessor, `get_items()`, so the column is built from the
toolbar's actual items rather than a list of names copied out of
`_init_main_toolbar` — an item added upstream then turns up on iOS instead of
quietly going missing. The two new members on `GLCanvas3D` are member
*functions*: they change neither the class layout nor its vtable, which is the
trap 0379 had to undo after a conditionally compiled data member shifted every
field declared after it.

**Run 131 found the access specifier first:** the two
new members were anchored next to `_render_gizmos_overlay()`, which sits in a
private section, so the free function the toolbar view calls through could not
reach `orca_ios_run_toolbar_action`. They are `public:` now, with `private:`
restored immediately after, which is safe for the same reason they are member
functions at all — functions take no storage, so the data members either side
keep both their access and their offsets.

Worth recording what did *not* break: `ios_webview_support.mm` compiled clean
at 647/659 on that run, all of the tab bar, the toolbar and the plus. The
Objective-C was not the risky part; the C++ it hangs off was.

**And then the device found the real one — see below.**

### 0400 — a plus in the corner of the home page

The home page offers "new project" and "open project" as two tiles in the body
of the page, above the recent files — a web page laying out a desktop dialog,
two large targets competing with the content for the top of the screen, in a
place iPadOS puts nothing. The tiles are hidden by `css/orca-ios.css` and a
plus goes in the top trailing corner instead, opening a menu with the same two
entries.

The menu is a real `UIMenu` on a `UIButton` with `showsMenuAsPrimaryAction`, so
the system draws it — its own glass, its own dismissal, its own pointer and
keyboard handling, its own accessibility. Nothing here paints a popup. One tap
opens it rather than a long press, because there is no second thing the button
could do and a hidden-behind-a-long-press control is one nobody finds.

Both entries end in the page's own handlers, `OnClickNewProject()` and
`OnClickOpenProject()` — the very functions the tiles called — reached with
`evaluateJavaScript:`. Whatever those do keeps working and this side does not
need to know what it is. Deleting the `#MenuArea` block in the stylesheet
brings the tiles back with nothing else to undo.

Two details worth keeping:

- **The web view is held in a file static, not captured by the menu's blocks.**
  A block capturing it would be retained by the action, which is retained by
  the menu, which is retained by the button, which is a subview of the very
  view tree the web view belongs to. This file is compiled without ARC, so
  that cycle would be permanent.
- **The button is constrained, not framed.** It is the one thing here that has
  to survive rotation and the split view on its own, and wx never lays out a
  view it did not create. Only the subview is constrained — its host keeps the
  manual layout wx gives it, which is allowed.

The titles come from `MainFrame::init_tabpanel` already translated: `_L()` is
not reachable from the .mm, and "New Project" is not called that everywhere.

### What run 131 crashed on, and what the next run will say

The build after the access fix launched on the iPad and died before the window
was usable: `SIGABRT`, `abort()`, out of

    wxAppConsoleBase::ProcessPendingEvents()
      wxEvtHandler::ProcessPendingEvents()
        wxEvtHandler::WXConsumeException()
          wxAbort()

That stack is wx aborting on a C++ or Objective-C exception that escaped an
event handler — and `ProcessPendingEvents()` is exactly where `CallAfter`
lambdas run, which is how the tab bar installs itself.

**The log localises it further, by what is missing.** `orca-ios-home: plus
installed` is there, so 0400 works. `orca-ios-tabbar:` appears **nowhere** —
neither the success line at the bottom of `orca_ios_install_tab_bar` nor the
"no root view controller" line above it. The success line is the last statement
in that function. So the throw was inside it, among the four UIKit containment
calls, and nothing recorded which one.

Two faults are visible without guessing which call threw:

- **The controller took the window with no tabs.** `syncTabs` only ran after
  the install returned, so a `UITabBarController` became the window's root
  while `tabs` was still empty, and loaded its view in that state. Tabs are
  built first now, before it goes anywhere near the window.
- **The deferred retry was queued once per call and logged nothing.** The
  notebook syncs on every page insertion and every selection change, so run 131
  queued roughly ten lambdas all racing to install the same thing, and the
  no-window path was silent, so the log cannot even say how many ran. One
  pending retry now, and it says so.

**The rest is instrumentation, deliberately, rather than a third guess.** Every
UIKit call in the install path logs what it is about to do (`step 1` … `step 5`),
and the whole function — plus `syncTabs`, plus the toolbar's commit — is inside
`@try/@catch`. An `NSException` is named, its reason logged, and a latch stops
further attempts. A throw now costs the native tab bar, not the application.

The catch also **puts wx back**: surviving is not sufficient on its own, because
a throw at step 4 or 5 leaves wx's content controller adopted by a bar that
never took the window — every view in the application parented to something
nobody is showing. The rollback detaches it and hands the window back, so the
app comes up exactly as it did before any of this, with the log saying what to
fix.

Next run either works or names the call and the reason. It will not be a
mystery twice.

### What the device showed next, and what was fixed

The build came up. The toolbar works — the column on the trailing edge renders
the nine collapsed items and the chevron, from the same SVGs, in glass. The home
page hides what it should. Three things were wrong.

**The plus was invisible.** It sits on the home page's web view, which is a
sheet of white, and `UIGlassEffect` over white is a circle nobody can see. It is
a plain translucent grey chip now — the same `colorWithWhite:0.5 alpha:` the
keypad in 0381 uses for its keys, so it reads on white and on dark alike. The
panels that float over the 3D view stay glass; there is something behind those
to refract. **Glass needs a background. Do not put it on the web views.**

**Three toolbars were still drawing across the top.** With the main toolbar and
the gizmo bar gone, what remained was a separator line dividing nothing from
nothing, the assemble-view button alone in a corner, and the sidebar toggle over
the top left of the plate — all of them positioned relative to the two bars that
are no longer there. `_render_separator_toolbar_left/right`,
`_render_assemble_view_toolbar` and `_render_collapse_toolbar` return early now,
and the last two have their hit rectangles skipped with the same
`orca_ios_toolbar_takes_mouse` switch the main toolbar uses.

The sidebar toggle is **not** lost with them: `get_collapse_toolbar()` is an
ordinary `GLToolbar`, so its items are collected into the native column exactly
like the main toolbar's, in the collapsed set — hiding the sidebar is the first
thing anyone wants on a screen this size. The assemble-view button is the
obvious next one to move across.

**The tab bar: the log named it outright.**

    orca-ios-tabbar: step 3, adopting the wx controller
    orca-ios-tabbar: THREW UIViewControllerHierarchyInconsistency: adding a
      root view controller <wxUIContentViewController: 0x11abb8000> as a child
      of view controller:<OrcaTabBarController: 0x11f952400>
    orca-ios-tabbar: wx has its window back

**A view controller must stop being a window's root before it can become
anybody's child.** The order was backwards, and the reason it was backwards is
worth keeping: the window holds the only strong reference to wx's content
controller, so clearing the root deallocates the object that owns every wx view
in the application. That is a real hazard — but the answer to it is a `retain`
of our own, not an adoption UIKit refuses. The window is handed over first now,
under our own retain, and the controller is adopted afterwards.

Note what the instrumentation bought: one run, one line, the exact call and the
exact reason, and the rollback meant the application came up perfectly usable
while being wrong. That is the whole argument for breadcrumbs over reasoning,
and it settled in one build what three rounds of thinking had not.

**And the menu is grey by forcing the interface style, not by painting one.** A
`UIMenu` is drawn by the system in the style of the view it is presented from,
so over the white home page it came up light however grey the button was.
`overrideUserInterfaceStyle = UIUserInterfaceStyleDark` on the chip carries into
the menu it opens — a grey chip with a grey menu, and still a real UIMenu with
the system's own dismissal, pointer handling and accessibility.

### Four tabs called Home — a trap in ButtonsListCtrl

The tab bar came up, with the right number of tabs and the right two held back,
and every one of them labelled "Home".

`Notebook::GetPageText(n)` forwards to `ButtonsListCtrl::GetPageText(n)`, and
that returns **the button's** label:

```cpp
wxString ButtonsListCtrl::GetPageText(size_t n) const
{
    Button* btn = m_pageButtons[n];
    return btn->GetLabel();
}
```

`SetCompact(n, true)` does `btn->SetLabel("")` — and the strip is compact on
this port always, because it is hidden and therefore has no width to fit a
label into. So every page returned an empty string, and the empty-label
fallback (which exists for the home page, the one page Orca really does insert
without a label) named all four of them Home.

Upstream keeps the real text in `m_pageLabels` for exactly this reason —
`SetCompact` reads it back to restore the label when the strip widens again.
`ButtonsListCtrl` gains a `GetPageLabel()` returning that, and `Notebook::GetPageText`
uses it on iOS only. Nothing about the desktop's behaviour changes.

**The general lesson: a getter named after a property is not always reading
that property.** The label was being asked of the widget that displays it
rather than of the model that owns it, and the widget had been told to display
nothing.

### Grey, in the menu

**The tab bar keeps the system style** — it was briefly forced dark and that was
not what was wanted. The grey belongs to the home page's menu.

A `UIMenu` is drawn by the system in the system's own material, and **there is
no API that tints it**. What the material follows is the interface style of the
view the menu is presented from, so over the white home page it drew light.
`overrideUserInterfaceStyle = UIUserInterfaceStyleDark`, set on the chip and on
the button that actually presents the menu, makes it draw the dark material —
which over a light page is the grey background wanted here.

That is the only public lever on that background. Anything more exact than "the
dark material" means drawing the popup by hand, and a hand-drawn popup gives up
the dismissal, the pointer and keyboard handling, the haptics and the
accessibility that come with the real one.

The home page's left column is hidden too. With the accounts and the OrcaCloud
shortcut gone it held one button — "Recent" — pointing at the only thing the
page shows, so it was a 262px border down the side saying nothing. `#RightBoard`
is already `width:100%`, so hiding `#LeftBoard` is the whole change.

### The plus: a circle with nothing in it, and nothing on tap

Both symptoms were one bug. The chip is laid out by constraints, so it starts
at `CGRectZero` and gets its size later. The button inside it was given a fixed
44x44 frame and a flexible autoresizing mask — and autoresizing a subview inside
a superview whose bounds go from `0x0` to `44x44` resizes it from a degenerate
starting size. The button ended up somewhere other than over the chip, so there
was no plus to see and the tap landed on the bare chip behind it.

The button is constrained to the chip's edges now. **Do not mix a constrained
parent with an autoresized child** — if the parent's size arrives from
constraints, the child's has to as well.

The tab bar reads Home / Prepare / Preview / Device, which is the whole of what
0397 set out to do.

### The plus had to leave wx's view tree

Three builds of "it draws but does nothing". The decisive symptom was the
second one: **no pointer hover either.** A wrong target/action gives you a
button that highlights and does nothing; no highlight and no hover means the
control is never hit-tested at all.

The evidence for where to put it was already in this repository. **The numeric
keypad in 0381 is the one native control on this port that has always worked,
and 0381 puts it in the window on purpose.** wx hangs a pan and a hover gesture
recogniser off every view it wraps (step2/0209 — the same pair that used to
kill the process on any touch), and a UIKit control buried underneath those does
not get a clean touch. In the window the chip is a sibling of everything wx
owns rather than a guest inside it.

So the chip is added to the `UIWindow` and constrained to its safe area. The
price is that the window is not the home page, so visibility has to follow the
notebook: `orca_ios_home_plus_update()` runs from `orca_ios_notebook_sync`,
which fires on every page change, and shows the chip only while the home page
is selected. That call sits **before** the tab bar's own early return on
purpose — the plus must not stop working because the tab bar failed.

`orca_ios_notebook_sync` is also the install's retry now, so the plus no longer
depends on a single `CallAfter` landing after the window exists.

**If a native control on this port does not respond, ask what it is parented
to before you look at its action.**

### And then the plus vanished: one retry loop, not one per feature

Moving the chip into the window was right, and it disappeared completely,
because of the retry rather than the move.

`orca_ios_notebook_sync` queued a `CallAfter` that installed **the tab bar** and
nothing else — and that `CallAfter` is the only thing on this path that runs
after the window exists, because the page changes are all over by then. So the
tab bar installed from it, the plus never did, and the plus needs a window now
in a way it did not when it lived in the page.

The retry re-enters `orca_ios_notebook_sync` itself now, so everything still
missing gets another turn, and it is bounded at sixty turns so something that
can never install cannot spin the event loop for the life of the process.

**Whatever runs after the window appears has to retry everything**, not the one
thing that was being debugged when it was written.

Worth checking next: the toolbar is still parented into the GL canvas's wx
view, which is the same position the plus was in when nothing reached it. It
draws, but whether its buttons take a tap has not been confirmed. If they do
not, the fix is the one above — into the window, visibility driven from the
notebook.

### Taking the window throws away what was already in it

The plus installed, and then vanished, and the log timed it to the microsecond:

    14.756740  orca-ios-home: plus installed in the window
    14.756768  orca-ios-tabbar: step 1, allocating ...
    14.819877  orca-ios-tabbar: step 3, taking the window
    14.869439  orca-ios-tabbar: installed

**`window.rootViewController = bar` replaces the window's subviews.** A chip
added to the window 28 microseconds earlier goes with them. Both of the last
two rounds were the same shape: the plus arriving *before* the thing that
rearranges everything underneath it.

Two changes, and between them the ordering stops mattering:

- **The host follows whoever owns the window.** `orca_ios_plus_host()` returns
  the tab bar controller's view once there is one, and the window until then.
  Either is outside wx's view tree, which is the whole point of the move.
- **The install repairs itself.** If the chip exists but its superview is not
  the host it should have — exactly what a window takeover leaves behind — it
  is torn down and rebuilt against the right one, and says so in the log.

The plus is now updated *after* the tab bar install rather than before, so on
the turn the bar arrives the chip is built against the bar's view straight away
instead of into a window that is about to be emptied. It still runs
unconditionally, outside the tab bar's early return, so it cannot be taken down
by the tab bar failing.

### The inset belongs to one page, not to all of them

Insetting wx's view for the floating bar was applied to every page, and it took
the toolbar off Prepare with it: the column is centred vertically in the canvas,
so moving the canvas down moved the column down past the bottom of the screen.
One change, two complaints, one cause.

**Only the Device page needs the room.** It lays its own header — Camera /
Control / Printer Parts — straight across the top. The 3D pages do not: a bar
floating over the empty space above the plate is the point of a floating bar.
`orca_ios_inset_tab()` names the page from `init_tabpanel`, by identity, the
same way the untabbed pages are named, and `-viewDidLayoutSubviews` applies the
inset only while that page is the selected one. A selection change asks for
layout again, because the inset depends on which page is showing.

The toolbar also logs its frame and its host's bounds once per run now, so a
column that is off the bottom of the screen can be told from one that was never
built.

### The plus belongs to the home page, and a new project belongs in Prepare

Three changes, all hanging off the same moment — the page on screen changing.

**The plus is home-page only.** It lives in the window, so nothing hides it on
its own; the visibility update now also runs from the tab bar's own
`pageDidAppear:`, not only from the notebook's page-changed event, and logs
which way it went so "it is still there on Prepare" is answerable from a log
rather than a screenshot.

**A new project switches to Prepare.** Making one and being left on the home
page is a dead end on this screen — there is nothing to do next. The menu action
runs the page's `OnClickNewProject()` and then, a turn later so the handler has
actually made the project, calls `select_tab(tp3DEditor)`.

**The 3D page can come up blank**, and main's own counters already name the
state: `no render; onscreen yes; (plater selected: not rendering is WRONG)`.
The canvas is shown and nothing has asked it to draw. Moving pages through the
native tab bar is one way in, because the bar changes what is on screen without
wx noticing anything happened. Every selection the bar makes now ends in
`orca_ios_wake_canvas()` — `set_as_dirty()` and a `Refresh()` on the current
canvas.

All three are hung off one scope-exit hook in `pageDidAppear:` so that the early
returns in that method cannot skip them.

**Autosave was not touched.** 0366 already autosaves into Documents and 0369
already puts autosaves in Recent, so placing an object with no project open
should save itself once the blank viewport is fixed and there is a way to place
one. That is worth checking on the next device run rather than adding a second
mechanism beside the one already there.

### 0401 — Slice and Print, in glass

**Hiding the tab strip hid the slice button.** Orca builds Slice and Print in
`MainFrame::create_side_tools()` and hands that sizer to `ButtonsListCtrl` —
the strip. 0397 hides the strip, so the most important control in the
application went with it. Anything else living in `side_tools` would have gone
the same way; that sizer is worth remembering.

They come back as their own glass capsule at the top trailing edge, and they
are still the same buttons underneath:

- **Label and enabled state are read from `m_slice_btn` / `m_print_btn`** every
  idle, so "Slice plate" turning into "Slice all", or greying out while a slice
  runs, carries across without this code knowing any of it.
- **`IsShown()` is respected rather than second-guessed** — that is Orca's own
  decision about whether the pair belongs on the current tab, and it is why the
  capsule disappears on Home and Device without a page test of its own.
- **A tap posts `wxEVT_BUTTON` to the button itself**, so everything bound to
  it — exiting the gizmo, choosing the plate, the filament group popup — runs
  exactly as on the desktop.

Hosted alongside the plus, outside wx's view tree, for the reason established
the hard way in 0400.

**Not yet moved across:** the two dropdown option buttons
(`m_slice_option_btn` / `m_print_option_btn`), which choose plate-versus-all.
Their popups are positioned against the hidden wx buttons and would open off
screen, so they need a `UIMenu` of their own on each capsule button.
## Will 0392/0393 give 120 fps? No — and the toolpaths are not what stops it

Asked before run 147 was tested on device, which is the right time to ask it:
the answer does not depend on the device numbers, and it changes what is worth
building next.

**120 fps is an 8.33 ms frame.** Three independent ceilings sit below that, and
everything shipped so far — 0390 through 0396 — attacks only the third.

### Ceiling 1: the frame without any G-code in it

From the run-132 log, `(render_ms 4838 - gcode_ms 4815) / 2 frames` = **11.5 ms
per frame** outside `GCodeViewer::render()`. Bed, objects, ImGui, overlays. If
the toolpath draw became literally free, the frame is still 11.5 ms — 87 fps of
work, and that is before the pacing loss below.

The independent check is Prepare, measured on the same drawable and the same
device: **201 frames in five seconds, 16 ms each**. Prepare draws no toolpaths
at all and is already **1.9x over the 120 fps budget**. Preview draws everything
Prepare draws, plus shells, plus the legend, plus the toolpaths. It cannot be
faster than the tab that does less.

Some of the 11.5 ms is already gone unmeasured: 0399 made `_render_main_toolbar`
and `_render_gizmos_overlay` return early. How much is unknown until
`legend_ms` / `seq_ms` / `slider_ms` come back from a device.

### Ceiling 2: wxIdle pacing

201 frames in five seconds out of 16 ms frames is **40 fps delivered from 62 fps
of work** — a third of the wall clock thrown away by `RequestMore()`. No amount
of cheaper frames raises this. Nothing else in the app can dispatch 120 frames a
second, so until the pacing changes, 120 is unreachable by construction and the
other two ceilings are academic.

### Ceiling 3: the toolpath draw

The one the shipped stack addresses. 0393 cuts vertices 3x (24 named corners →
8 shaded); 0392 cuts per-vertex cost (twelve integer divides, six `textureSize`,
half the varying) but **not the six texture fetches per vertex**, which are
dependent RGBA32F reads and are the likelier bottleneck. The collinear merge
ratio *n* is the only large unknown, and `MAX_RUN_SEGMENTS` caps it at 64.

At a generous 300M vertices/s, with 19.2M/*n* vertices a frame:

| *n* | toolpath | + 11.5 ms | work | delivered (x0.65) |
|---|---|---|---|---|
| 4 | 16 ms | 27.5 ms | 36 fps | ~24 |
| 8 | 8 ms | 19.5 ms | 51 fps | ~33 |
| 16 | 4 ms | 15.5 ms | 64 fps | ~42 |
| 32 | 2 ms | 13.5 ms | 74 fps | ~48 |
| free | 0 ms | 11.5 ms | 87 fps | ~57 |

The column saturates near **50 fps** whatever the merge ratio does. That is the
whole finding: as soon as the toolpaths get cheap, the fixed cost and the pacing
are the frame, and neither has been touched.

So run 147 should be a large win over 1 fps — the toolpath work is sound and
worth having. It is not a 120 fps build, and no tuning of *n* makes it one.

### What was wrong on the shelf, in our favour

The last round shelved the drawable fix as expensive:

> It needs a `wxGLCanvas` setter, so it is a `patches/step2` change and a 25
> minute wx rebuild.

It does not. `m_canvas->GetHandle()` already returns the GLKView — 0380 and 0399
both use exactly that. It is a **step3 change with no wx rebuild**.

It is also worth more than "halve it while moving", because the oversampling is
permanent rather than a moving-frame problem. `orca-ios-display` reports `scale
2, nativeScale 1.72`: the app renders at 2x and the compositor resamples down to
1.72x before anything reaches the glass. **(1.72/2)^2 = 74%** — a quarter of
every fragment, every tile store and every post-transform vertex binned has
always been discarded, at rest and moving alike. Drawing at `nativeScale` is not
a quality trade, because the pixels being dropped are ones the panel never had.

## 0402 — draw the pixels the panel actually has

`RetinaHelper::get_scale_factor()` now reports the panel's native scale, and
sets the GL canvas's `contentScaleFactor` to match on the way past.

The subtlety is not the setter, it is that **this number has three consumers
that have to agree**: the framebuffer-pixel size `get_canvas_size()` hands to
`glViewport`, the ImGui overlay scaling, and the pointer coordinates `on_mouse*`
multiply up before picking. Changing the view's scale while `RetinaHelper` kept
reporting wx's 2 would give a viewport 16% wider than the framebuffer it draws
into and picking wrong by the same margin — which would have read as a rendering
bug, not a scale bug.

The second subtlety is that the scale is answered from `UIScreen`, not read back
off the view. `GLCanvas3D` treats it as a constant — `update_ui_from_settings`
corrects camera zoom by `new/orig` whenever it changes, because zoom is in
pixels — so a value that moved under it would misplace the view and the touches
together. Reading the view back would do exactly that: `RetinaHelper` is
constructed with the wxGLCanvas, and any call made before its peer exists would
report 2 while every later call reported 1.72. Answering from the screen means
the first caller and the ten-thousandth get the same number whether or not there
is a view yet, and the view is corrected to match as soon as there is one. There
is no transition, so there is nothing to correct.

Nothing else needed changing: `[v bindDrawable]` already runs once per frame
(step2/0224) and rebuilds GLKView's framebuffer from the resized layer, and
`GLCanvas3D::render()` already re-reads the framebuffer name straight after
(`orca_ios_note_default_framebuffer`, 0329). Both of those exist for other
reasons and happen to make a mid-flight drawable resize safe.

`orca-ios-drawable:` in the log says it took effect, once.

### What to check on the next device run

* **`orca-ios-drawable:`** — should say `contentScaleFactor -> 1.72`.
* **The legend and sliders should be the same physical size.** ImGui is scaled
  by this factor (`GLCanvas3D.cpp:7500`), so it should render at 1.72x and
  occupy the same space. If the overlay changed size, that assumption is wrong
  and this patch is what did it.
* **Picking still lands where you touch.** Same factor, same reason.

### Still on the shelf, in order

1. **Drop the drawable further while moving.** 1.72 → 1.0 is another 3x of
   fragment and tile cost. Now cheap to reach, but no longer free of
   transitions: the scale would change mid-gesture, and camera zoom and pointer
   coordinates both depend on it. Needs the change confined to the settle
   boundaries of 0394's clock, and it needs 0401 to have proven safe first.
2. **A CADisplayLink instead of wxIdle pacing.** Ceiling 2, and the only route
   to more than 60 delivered frames. Also the riskiest change on the list.
3. **The 11.5 ms itself**, once the phase numbers say where it goes.

## 0403 — the idle sync was on a branch no platform compiles

Found while reading `on_idle` for the pacing work, not by looking for it.

0394 added the counted version of the dirty evaluation under
`#if defined(__APPLE__) && !TARGET_OS_OSX` and turned the original body into the
`#else`. 0399 then added `orca_ios_sync_toolbar()` to that `#else`, wrapped in
`#if defined(__APPLE__) && !TARGET_OS_OSX` — the same condition it is already
the negative arm of. 0401 added `orca_ios_sync_side_tools()` immediately after
it and inherited the position.

So the guard reads, in effect, `if (iOS) { ... } else { if (iOS) { sync } }`.
On iOS the first arm is taken and the sync is not compiled; everywhere else the
inner test is false. **Neither call has ever run on any platform.** The native
toolbar's item states and the Slice/Print capsule were never synced from idle —
they showed whatever they were built with, and "Slice plate" becoming "Slice
all", or greying out while a slice runs, could not have worked.

Moved into the arm iOS actually compiles, straight after
`m_main_toolbar.update_items_state()`, which is where the comment always said it
belonged.

Worth noting for the next person: this is invisible to CI. It compiles, links
and runs. Only the behaviour is missing, and only on a device.

## 0404 — pace the frame loop from the display

Ceiling 2 from the 120 fps analysis. wx processes idle from a run-loop observer
on `kCFRunLoopBeforeWaiting` (`src/osx/core/evtloop_cf.cpp`): once per run-loop
iteration, just before it would sleep. `on_idle` renders and calls
`evt.RequestMore()`, which makes `ProcessIdle()` return true, which calls
`WakeUp()` to stop the loop sleeping, which produces another iteration.

That is the entire frame loop, and it turns only while the previous frame
remembers to ask for another one. Under `UIApplicationMain` nothing reliably
does — 0364's complaint — and it is why Prepare delivers **40 frames a second
out of 16 ms of work**. The missing 22 fps are iterations that never happened.

A `CADisplayLink` fixes this by existing. **The callback does no rendering at
all** — it increments a counter. It does not need to do more: a display link is
a run-loop source, so the display waking the loop *is* an iteration, and every
iteration ends in the observer that runs idle. Firing at the refresh rate hands
wx one idle pass per displayed frame, through the path that already exists.
No second render path to keep in step with the first, nothing to re-enter, and
no change to `GLCanvas3D` at all.

`preferredFrameRateRange` asks for 120 and settles for 60. Both directions are
requests: the system drops the rate for thermals, and Low Power Mode pins
ProMotion to 60 for every app, which is not something an app can opt out of.
Added to `NSRunLoopCommonModes` so it keeps firing inside the nested run loops
wx spins for modal dialogs (step2/0216) — on the default mode alone the viewport
would freeze behind every modal, which is the state 0364 was written to escape.

The report gains `link +N`. At 120 Hz that should be ~600 in a five second
window, with `idle` no longer far below it. A frame rate that did not move and a
link that never started look identical in a log, so it is counted.

## Reducing resolution while moving cannot be done this way — shelf item 2 was wrong

Shelf item 2 has been "halve the drawable while moving" for three rounds, and
0402 made it look cheap: the GLKView is one `GetHandle()` away, so it seemed to
be a matter of dropping `contentScaleFactor` while `orca_ios_is_interacting()`
and putting it back on settle. It is not, and the reason is worth writing down
so nobody tries it a fourth time.

`RetinaHelper::get_scale_factor()` is not just a number the renderer reads. It
is wired, through `GLCanvas3D::_resize()`, into
`ImGuiWrapper::set_scaling(font_size, 1.0f, scale)` — and `set_scaling` does two
things that are fine once and unacceptable per gesture:

* **`destroy_font()`**, which deletes the glyph atlas texture and forces the
  whole atlas to be re-rasterised and re-uploaded on the next frame. Twice per
  gesture, at the moment the drag starts — exactly the frame this change exists
  to make cheap.
* **`ImGui::GetStyle().ScaleAllSizes(new / old)`**, which is cumulative and
  lossy. Every padding, rounding and spacing value is multiplied up and down
  again on every gesture, and the rounding drifts. After enough drags the
  overlay is visibly wrong and nothing points at why.

And if `_resize()` does *not* run on a scale change, ImGui keeps the old font
size against a different framebuffer and the legend changes apparent size on
every drag instead. There is no arrangement of the existing call graph where
this is correct.

The camera has the same shape of problem, already documented in 0402:
`apply_projection` derives the frustum from `m_viewport` in pixels
(`w = 0.5 * m_viewport[2] * inv_zoom`), so zoom has to be corrected by
`new/orig` at every transition, and the correction has to land *after*
`camera.apply_viewport()` or `min_zoom()` clamps against the previous
viewport — which bites precisely at "zoomed to fit", the most common state.
That part is solvable. The ImGui part is not, not from this lever.

**What it would actually take:** render the 3D scene into an offscreen FBO at
reduced size, upscale that to the drawable, and composite ImGui on top at full
resolution. That decouples scene resolution from `get_scale_factor()`, which is
the only way the overlay stays put. It is a real piece of work — a new render
target, a resolve pass, and integration with the picking pass and with the
GLKit default-framebuffer wrapper (0329) — and it should follow a measurement
that says fragment and tile cost are actually a large share of the moving
frame, which nothing has established yet.

This also retroactively justifies 0402's design. Answering the scale from
`UIScreen` so that no consumer ever observes a transition was chosen to keep the
camera from moving underneath. It turns out to be what keeps the font atlas from
being thrown away as well.

## Run 150, measured: the merge did nothing, and that is the whole story

```
render +5  render_ms 2674  gcode_ms 2673  shells_ms 3  paths_ms 2669  legend_ms 0
lod_stride 1  segs 1372290->1372290
idle +845  link +241  swap_ms 0
```

Five frames in five seconds. **534 ms a frame, and 99.8% of it is `paths_ms`.**

### What worked

* **The shader work paid, about as predicted.** Run 132 was 2407 ms for 2.4M
  segments — 1.00 us per segment. This is 534 ms for 1.37M — **0.389 us per
  segment, a 2.6x improvement**. Indexed templates and the addressing changes
  did what the arithmetic said they would.
* **The display link runs.** `link +600` in a five second window is 120 Hz on
  the nose, and `idle +2088` alongside it shows idle is not starved at all —
  it runs at ~450/s. Pacing was never going to be the binding constraint at
  534 ms a frame, and it is not. It will matter when frames are cheap.
* **Ceiling 1 is much smaller than estimated.** I put the non-G-code frame at
  11.5 ms from run 132. Measured here: `render_ms 37` over 8 frames on Prepare
  is ~4.6 ms, and `legend_ms`/`seq_ms`/`slider_ms` are 0. 0399 taking the GL
  toolbars out of the frame is most of the difference. The ImGui phases are not
  a problem and can come off the list.

### What did not

**`segs 1372290->1372290`.** The collinear merge produced a ratio of exactly
1.000 — not one run longer than a single segment, on a 1.37M segment toolpath
from a mesh dense enough that the slicer warned about it. Every estimate in the
last round's table was multiplied by this, and it is 1.

The cause is `same_appearance()`. It compared sixteen fields, nine of which are
per-move floats a slicer varies continuously. `actual_feedrate` alone is enough:
it is the acceleration-planned speed of each move, so along a curve made of
micro-moves no two consecutive vertices carry the same value, and every run was
cut before geometry was ever tested. The strictness was deliberate — it was
meant to make a merge survive any later change of view type — and it cost the
entire win.

## 0405 — merge on what the shader can actually see

Per segment the shader reads exactly three things: the two endpoint positions,
the height/width/angle triple, and the colour. Nothing else reaches the GPU, so
nothing else can change how a segment is drawn. The test is now:

* `height` and `width`, because they build the box;
* **the colour, taken live from `get_vertex_color()`** rather than from
  `color_id` — for most view types the colour is derived from one of the very
  scalars the test no longer compares, and it is derived in `update_colors()`,
  which runs *after* `update_enabled_entities()`, so the cached
  `m_vertices_colors` would be a frame stale on exactly the change that matters;
* `layer_id`, which the shader does not read but `update_colors_texture()` does:
  under `top_layer_only_view_range` the layers below the top are recoloured to
  `DUMMY_COLOR` per vertex, and a run spanning that boundary would take one
  colour for both sides of it;
* `role`, `type`, `extruder_id`, kept because they are discrete, never vary
  along a straight run, and are what the enabled set is filtered by.

Dropped: `feedrate`, `actual_feedrate`, `mm3_per_mm`, `fan_speed`,
`temperature`, `layer_duration`, `pressure_advance`, `acceleration`, `jerk`.
They reach the picture only through the colour, and the colour is now compared
directly.

Because the merge now depends on the colour, `set_view_type()` and
`set_time_mode()` set `update_enabled_entities` as well as `update_colors`, so a
recolour re-derives the runs instead of repainting merged ones. That is the
`O(vertices)` rebuild, which is why it hangs off a deliberate user action and
nothing that happens per frame.

### And the counters that should have been there the first time

The last round shipped a merge whose ratio came back exactly 1.000, and the log
could not say which of four conditions was doing it. That cost a whole build.
The report now carries `cut g<N> a<N> m<N> c<N>` — runs ended by a gap in the
enabled set, by appearance, by geometry, or by `MAX_RUN_SEGMENTS`. Whichever
dominates is what decides the merge ratio, and so the size of the frame.

If `a` still dominates, something in the narrowed test is still varying and the
colour is the only real candidate. If `m` dominates, the toolpath genuinely is
not collinear at 0.01 mm and the epsilon is the dial. If `c` dominates, the
merge is working and `MAX_RUN_SEGMENTS` (64) is the cap — raise it.

### One diagnostic gap of my own

0402 logs `orca-ios-drawable:` only when it *changes* `contentScaleFactor`, so
silence means either "already correct" or "never applied" and the log cannot
tell them apart. `orca-ios-gl: first render() canvas=2070x2021` is the same
width as the 2070 recorded before 0402, which suggests the factor is still 2 —
but the layout changed under 0397-0401 too, so that is not proof either.

It does not matter much yet: at 534 ms a frame with 99.8% in `paths_ms`, a 26%
cut in fragment and tile cost is not the lever. It should be logged
unconditionally next time it is touched.

## Run 151: the counters earned their place immediately

```
segs 1372290->846372   cut g758093  a87814  m464  c0
paths_ms 3269 over 10 renders -> 327 ms a frame
```

| cut by | runs | share |
|---|---|---|
| gap in the enabled list | 758 093 | **89.6%** |
| appearance | 87 814 | 10.4% |
| geometry | 464 | **0.1%** |
| MAX_RUN_SEGMENTS | 0 | 0% |

Two things fall straight out of that table.

**The toolpath is collinear.** 464 geometric cuts in 1.37M segments settles the
premise the whole merge was built on — the mesh really does over-sample straight
lines, and 0.01 mm is nowhere near the binding constraint. The epsilon is not
the dial and never was.

**The index list is shredded.** An average run of 1.62 segments, with 90% of
runs ended by a hole rather than by anything about the geometry. Narrowing
`same_appearance` was right — it went from cutting 100% of runs to 10% — but it
uncovered the real limiter rather than being it.

The merge is worth 1.62x, so 534 ms became 327 ms. Still ~3 fps.

## 0406 — step over the holes nothing moved across

`Retract`, `Unretract` and `Seam` are move types emitted **at a point**: the
nozzle does not move. On top of being options in their own right, they also
invalidate the lines either side of them, because `extract_pos_and_or_hwa()`
only accepts a line whose two ends share a move type:

```cpp
const bool this_line_valid = i + 1 < vertices.size() &&
                             vertices[i + 1].position != v.position &&
                             vertices[i + 1].type == move_type &&
                             move_type != EMoveType::Seam;
```

So one seam at the end of a perimeter loop removes three consecutive indices
while the extruder stands still, and a model with a loop per island per layer
has hundreds of thousands of them.

Stepping over a hole like that is **exact, not approximate**: if every skipped
vertex sits at the same position, the merged chord passes through an identical
set of points and draws no line that was not drawn before. A hidden *travel* has
real spatial extent and fails the test, so the one thing that must never happen
— a chord painted across open air where the nozzle moved without extruding —
cannot. That is the whole safety argument, and it rests on a position
comparison rather than on a list of move types to trust.

The report gains `br<N>` (holes stepped over) and `gv<N>` (vertices lost to the
holes that could not be). **`gv / g` is the diagnostic**: about 1-2 means the
remaining gaps are markers and something is still wrong with the bridge; a large
number means they are genuine travels, which are supposed to cut runs, and the
merge is then doing everything it can.

### What this is worth, and what it still will not reach

If the gap cuts collapse to near zero, the runs are bounded by appearance
(87 814) and geometry (464) — about **15.5x**, or ~34 ms a frame at the
currently measured cost per segment.

That is ~29 fps, not 120, and the gap matters: **the per-segment cost has not
moved at all.** 0.389 us before the merge, 0.386 us after — the merge makes the
frame smaller without making a segment cheaper. 846 372 drawn segments at 8
vertices each is 6.8M vertices in 327 ms, which is **20.7M vertices/sec** on an
M4. That is roughly twenty times slower than the part should manage, and it is
the same signature as before: `swap_ms` at 0, `worst_ms` barely above the mean,
cost linear in segment count.

So there are two independent problems and only one of them is being worked:

1. **The scene is too big.** The merge addresses this, and 0406 is the rest of
   it. A perfect merge lands around 29 fps on the current cost curve.
2. **Each segment costs ~20x what it should.** Untouched, unexplained. If the
   frame is being split into partial renders, dropping under that threshold
   would take the cost per segment down with it and 1 would be worth far more
   than 15.5x — the win would be superlinear. If it is not partial renders, this
   needs its own investigation, and instancing is the first suspect: 846k
   instances of 8 vertices is a shape Apple's tiler is known to dislike, and
   `gl_VertexID`-based addressing of one flat draw would remove instancing
   entirely without changing a pixel.

Number 2 is where the next round should go if 0406 lands and the frame is still
tens of milliseconds.

## Run 152: the bridge worked, and it changed nothing

```
segs 1372290->834850  cut g23009 a257979 m553861 c0  br735084 gv290665
```

| cut by | run 151 | run 152 |
|---|---|---|
| gap | 758 093 | **23 009** |
| appearance | 87 814 | 257 979 |
| geometry | 464 | **553 861** |

`br735084` — the holes were stepped over, 735 084 of them, and gap cuts fell by
97%. `gv/g` is 12.6 vertices per surviving gap, so what remains are genuine
travels, which are supposed to cut runs. That part of the diagnosis was right
and is now finished.

The merge went from 1.62x to 1.64x. Nothing.

### The correction: the toolpath was never shown to be collinear

Last round's note said "464 geometric cuts in 1.37M segments settles the
premise — the toolpath really is collinear". **That was wrong, and the error
was in the reading, not the data.** The gap test runs before the geometry test
and `break`s, so geometry was never reached on 90% of runs. It was masked, not
absent. With the holes bridged it becomes the dominant cut at 553 861 — 66% of
all runs.

The lesson is about the counters, not the toolpath: a counter placed after an
earlier `break` measures only what survives to it, and reading it as a
population statistic is a mistake. `m` was a conditional probability all along.

### Both survivors are exact float comparisons

* **Geometry, 553 861.** `COLLINEAR_EPS_MM` was a flat 0.01 mm — a fortieth of a
  0.4 mm extrusion. But the thing being drawn is a *ribbon 0.4 mm wide*, so a
  chord that stays within an eighth of the width never leaves it, at any zoom.
  The bound is a fraction of the line the deviation hides inside, not a distance
  in millimetres.
* **Appearance, 257 979.** `height` and `width` were compared with `==`, and
  Orca varies extrusion width continuously — Arachne fits each wall to the space
  it has, so width drifts along a perimeter. Exact equality cut a run wherever
  the wall got a micrometre thinner.

0407 makes both relative: `collinear_eps(width) = max(0.01, 0.125 * width)`, and
height/width equal within 2%.

## The other half, which the merge cannot fix

Cost per drawn segment, three runs:

| run | drawn | ms/frame | us/segment |
|---|---|---|---|
| 150 | 1 372 290 | 534 | 0.389 |
| 151 | 846 372 | 327 | 0.386 |
| 152 | 834 850 | ~400 (worst_ms 411) | ~0.48 |

**The cost is linear in segment count and the merge does not make a segment
cheaper.** 150 and 151 agree to within 1% across a 1.6x change in count. So the
merge is worth exactly its ratio and nothing more, and an 8.3 ms frame needs
about 21 500 drawn segments — a 64x merge, which is exactly `MAX_RUN_SEGMENTS`.
Not reachable. Even a perfect merge lands near 30 fps.

6.8M vertices in 327 ms is **20.7M vertices/sec** on an M4. That is one to two
orders of magnitude below what the part should do, and it has not moved through
any of this.

### 0407 also stops the guessing about why

Three rounds have been spent reasoning about that number from frame times that
include CPU submission and whatever the driver blocked on. So it is measured
directly, once: the same geometry is drawn twice more, back to back, `glFinish`
either side, at the real viewport and at a sixteenth of the area. Both land in a
frame that the real draw then overdraws, so nothing shows; it costs one slow
frame at load.

```
benchfull <ms>  benchquarter <ms>
```

* **benchquarter ≈ benchfull** → the frame is bound by the vertex stage.
  Resolution is irrelevant, and only fewer or cheaper vertices help. The next
  lever is the shape of the draw: 835k instances of 8 vertices is a shape Apple's
  tiler handles badly, and a flat `gl_VertexID` draw would remove instancing
  without changing a pixel.
* **benchquarter << benchfull** → the frame is bound by fragment and tile-store
  work, and the answer is an offscreen target at reduced resolution — the option
  0405's note said needed a measurement before anyone built it. This is that
  measurement.

Either way the next round stops being a guess, which the last three were.

## Run 153: the benchmark answered the architecture question

```
benchfull 274 ms   benchquarter 262 ms
segs 1373315->623307   cut g22995 a63803 m536508 c0
```

**A sixteenth of the area costs 96% of the full-viewport time.** The frame is
bound by the vertex stage and by nothing else. Fragments, tile store, resolution
— together they are 4% of it.

That closes two things at once:

* **The offscreen-render-at-reduced-resolution idea is dead**, and with it the
  last of the drawable work. 0405's note said it needed a measurement before
  anyone built it; the measurement says it would have bought 4%. Not building it
  was the right call, and 0402's 26% is worth almost nothing on this frame too.
* **Only fewer or cheaper vertices can help.** Everything else is noise.

### The tolerances: half worked

| cut by | run 152 | run 153 |
|---|---|---|
| appearance | 257 979 | **63 803** |
| geometry | 553 861 | 536 508 |

Relative width matching was right — appearance cuts fell 4x, and the merge went
1.64x to **2.20x**. Arachne's drifting wall width was exactly the problem.

Raising the collinearity bound 5x moved geometry by 3%. That is a flat response,
and it means the path really does turn: **this toolpath is not a set of
over-sampled straight lines.** The premise the whole merge was built on holds
for a smooth dense mesh and does not hold for this model. The merge is near its
ceiling at ~2.2x, and it is no longer where the frame is.

### The number that is left

623 307 segments x 8 vertices = 5.0M vertices in 274 ms — **18.2M vertices/sec**
on an M4. One to two orders of magnitude below what the part should do, linear
in segment count, and unmoved by everything so far.

440 ns per instance, for 8 vertices. Across a 10-core GPU that is close to no
parallelism at all, which is not what expensive per-vertex work looks like — it
is what serialisation looks like. Two candidates, and they call for opposite
fixes:

1. **The shader.** Six texture fetches with a dependent chain: index texture ->
   positions -> height_width_angle, the last addressed by `closer_id`, which is
   computed from the fetched positions. Latency that cannot be hidden.
2. **The draw's shape.** 623 307 instances of 8 vertices, via
   `glDrawElementsInstanced` with 24 `GL_UNSIGNED_BYTE` indices. Tiny instances
   in enormous numbers is a shape Apple's tiler is known to handle badly.

## 0408 — shorten the chain, and price the shape

**The fix, for candidate 1.** `height_width_angle` is now fetched for both
endpoints and selected between, instead of fetching `[id]` and then
`[closer_id]`. Same two fetches, but both are addressable the moment the index
fetch lands, so the chain is two levels deep rather than three and the pair is
issued together. `closer_height_width_angle` becomes a register select.

**The measurement, for candidate 2.** A control program: the same draw call, the
same instance count, the same vertices per instance, no fetches, no lighting, no
matrices — position synthesised from `gl_InstanceID`. Real triangles, so nothing
is culled before it is counted. Timed in the same one-shot fenced benchmark:

```
benchfull <ms>  benchquarter <ms>  benchnull <ms>
```

* **benchnull ≈ benchfull** → the shape is the cost, the shader is irrelevant,
  and instancing has to go: a flat `gl_VertexID` draw computing segment and
  corner arithmetically, same pixels, no instancing.
* **benchnull ≪ benchfull** → the fetches are the cost, and the shader is where
  to work: pack colour into the position texture's unused `.w`, and consider
  dropping FIX_TWISTING on a tablet.

The control skips the texture binds and the `data_shift` / `index_shift`
uniforms, because those locations were queried from the real program and setting
them while another program is current is an error rather than a no-op.

## Run 154: it is not the shader, it is the shape

```
benchfull 507   benchquarter 267   benchnull 236
```

`benchfull` is the first of the three draws and carries the warm-up; 267 is the
warm number for the real shader, and it matches run 153's 262.

**`benchnull` is 236 ms** — the same draw call, the same 622 894 instances, the
same 8 vertices each, with a vertex shader that fetches nothing, lights nothing
and multiplies by nothing.

| | ms | share |
|---|---|---|
| shader work (fetches, lighting, matrices) | 31 | **12%** |
| the shape of the draw | 236 | **88%** |

**379 nanoseconds per instance to draw eight vertices of nothing at all.** Every
round of shader work — the indexed templates, the power-of-two addressing, the
mediump varying, this round's shortened fetch chain — has been optimising 12% of
the frame. That is why the frame time has not moved: 0408's dependency fix was
correct and bought nothing, exactly as this number predicts.

The frame is unchanged at 241 ms, as expected.

## 0409 — 16-bit indices, and a price for instancing

**The fix that costs nothing to try.** The segment template draws with
`GL_UNSIGNED_BYTE` indices. Almost no GPU has native 8-bit index fetch; the
usual driver response is to convert the buffer to 16-bit, and a driver doing
that per draw — or per instance — produces exactly this signature: a
per-instance cost that does not care what the shader does. `INDEX_DATA` is
`uint16_t` now and the draw passes `GL_UNSIGNED_SHORT`. Twenty-four extra bytes,
once.

**The measurement, if it is not that.** `benchflat`: the same control geometry
drawn with no instancing, no index buffer and no vertex attributes — `24 * N`
vertices from one `glDrawArrays`, segment and corner derived from
`gl_VertexID`, into an empty VAO (this VAO's attribute 0 is an eight-element
buffer and 24N vertices would read off the end of it).

* **benchflat ≪ benchnull** → instancing is the cost. The toolpath gets drawn
  flat: 24 invocations per segment instead of 8, no per-instance cost, same
  pixels. On these numbers that trades 236 ms of instancing for 24 x 6.2 ns x
  622 894 = ~93 ms of vertex work, which is a 2.5x frame at worst and better if
  the per-vertex cost falls outside an instanced draw.
* **benchflat ≈ benchnull** → the cost is per-vertex dispatch and neither shape
  wins. Then the only remaining lever is fewer vertices, and that means a real
  vertex buffer built once at load instead of the texture-fetch expansion —
  which is what a normal renderer does and what libvgcode's design has been
  avoiding all along.

### What the numbers say about the target

Being honest about the ceiling: at 6.2 ns per vertex, 120 fps (8.3 ms) affords
about 1.3M vertices a frame. Flat drawing needs 24 per segment, so ~56 000
segments; the merge gives 623 000. Even with instancing gone entirely, this model
at full detail is roughly an order of magnitude past a 120 Hz frame, and about
2-3x past 60. The honest targets are a solid 30 while moving on a model this
size, 60 on ordinary ones, and 120 only on small ones — unless the vertex count
per segment comes down, which is the vertex-buffer question above.

### 0410 — the tab bar follows the notebook from the idle

Of the three things reported together — no Slice button, no toolbar, and a tab
bar reading Home while Prepare was on screen — **0403 accounts for the first
two**: the syncs had been written into the `#else` arm of an
`__APPLE__ && !TARGET_OS_OSX` block, so they were unreachable on every platform.
Nothing was wrong with the toolbar or the capsule; they were simply never told
anything after they were built.

The third is separate and is what this patch is for. The bar is updated from
`wxEVT_BOOKCTRL_PAGE_CHANGED`, and that does not carry every page change —
`select_tab()` from the New Project menu is one that it misses, which is why the
bar stayed on Home. `orca_ios_native_tick()` re-reads the notebook every idle,
so no route can be missed, and the canvas wake hangs off the same selection
comparison rather than off a tap on the bar.

**It sits above `m_initialized`, and the toolbar sync deliberately does not.**
The toolbar reads state that only exists once the canvas is up, so under the
guard is right for it. The tab bar reads the notebook, which is there either
way — and a canvas that has never rendered is exactly the blank Prepare page,
so anything the bar needs must not be downstream of that guard.

Two things worth carrying forward from this round:

- **Check which arm of an `#if` a hook lands in.** The generator anchors on
  source text, and main has restructured `on_idle` into `#if`/`#else` more than
  once; matching text inside the wrong arm compiles silently and does nothing.
  The verification for this patch walks the preprocessor stack down to the call
  and prints the arm rather than trusting the anchor.
- **The generator writes its file only at the end.** A failed assertion part way
  through leaves the patch generated from a partly-edited tree, which is how a
  half-written 0399 was produced earlier in this round. Regenerate from scratch
  after any script change and re-verify the whole stack.

### 0411 — the strays, the dropdowns, the hover and the room

**The grey bar over the sidebar was two orphaned panels.**
`create_side_tools()` parents `slice_panel` and `print_panel` to the **frame**,
not to the tab strip — only their *sizer* goes into the strip. Hiding the strip
therefore left two dark grey panels as stray children of MainFrame, unpositioned,
sitting at the top left. They are hidden now. Their buttons keep their own
`IsShown()` flag, because `wxWindow::IsShown()` reports that window alone, so
what the capsule reads to decide whether Orca wants the pair on this tab is
undisturbed.

**The dropdowns do not do anything by themselves.** Orca's little chevron sets
`m_slice_select` / `m_print_select` and the main button then acts on that mode.
So each capsule button carries a `UIMenu` with the two entries, which set the
same field and fall through to the same click — "Slice all" from the menu is
identical to choosing it from the real dropdown and pressing Slice.

**The toolbar icons had no pointer effect** — `pointerInteractionEnabled` was
set on the plus and on the capsule but never on the column's own buttons.

**And the column opened underneath the capsule.** Both live in the trailing
corner; the column is centred vertically, so expanded it ran up under the Slice
capsule and lost its top icons. It starts below the capsule now and takes its
available height from there.

### 0412 — German

The device's locale is German, and every launch log still said `Cannot set
locale to language "English (Switzerland)"` — wxLocale cannot set the regional
locale this iPad reports, so `load_language`'s system-language branch fell
through to English. Naming the language outright skips that guess: the catalog
asked for is `resources/i18n/de`, which ships in the bundle, and wxTranslations
loads it whether or not the C locale can be set. It is written back to the
config so Preferences shows what is in use and later launches take the ordinary
path.

**Still not done: the sliced-info box under the Slice button.** Moving Orca's
sliced-info panel out of the sidebar means either reparenting a wx panel into a
floating position — which is the class of thing that has gone wrong repeatedly
here — or reading its values and drawing them in the capsule. The second is the
right shape and is the next piece of work.

### 0413 — the Device page makes its own room

The floating bar covers the top of the window, and the Device page lays its own
header across it — but insetting the *whole* page pushed the vertical strip of
tabs down its leading edge as well, and that strip has nothing above it: the bar
is centred, the strip is at the edge.

So the inset moved into `Tabbook::DoSize`, onto the **page rect alone**.
`m_bookctrl` — the vertical strip — is positioned by the sizer and keeps the
top of the window. The tab bar controller no longer insets wx's view at all,
`orca_ios_inset_tab()` is no longer called, and the height itself went from 56
to 72.

`Tabbook.hpp` did not include `TargetConditionals.h`. Without it `TARGET_OS_OSX`
reads as zero and the iOS branch compiles into the macOS build — the same trap
0372 documents for Notebook.hpp, and worth checking in every header this stack
reaches into.

### 0414 — the grey band, and the system appearance

**Hiding the tab strip was not enough to remove it.** `m_bookctrl->Hide()` stops
it drawing and takes it out of `GetControllerSize()`, so the pages get the whole
window — but the sizer *item* it sits in still contributes its border,
`m_controlMargin` all round, and the notebook's own grey shows through that band
across the top of the page. That is the grey bar, and it survived hiding the two
orphaned side-tool panels in 0411 because it was never those panels. The item is
not added to `mainSizer` at all on iOS now. `m_controlSizer` is still built and
still owns `m_bookctrl`, so nothing that reaches for either changes.

**Dark mode was compiled out, not broken.** `SUPPORT_DARK_MODE` is defined
nowhere in this tree, so `GUI_App::dark_mode()` reduces to `return false` and
`mac_dark_mode()` — the iOS stub of which returned `false` as well — was never
even consulted. Every colour Orca picks goes through `dark_mode()`, so answering
it honestly is the whole change: on iOS it returns the device's current
`userInterfaceStyle`. The stub delegates to the same read, and
`traitCollectionDidChange:` on the tab bar controller re-asks the colours and
repaints when the device switches.

Nothing is cached. `dark_mode()` is a live read, so a switch needs only a
recompute and a repaint, not a stored flag that could disagree with the system.

**On the toolbar's hover:** `pointerInteractionEnabled` is set on every button
in the column. Only some of them light up because only some are *enabled* — with
no model on the plate, most tools are disabled, and a disabled button does not
take a pointer effect. That is correct behaviour rather than a gap; load a model
and the rest light up.
## 0410 — draw the toolpath flat

Run 156 corrected run 155: `benchflat` was **18 ms**, not 146. The 146 was the
first use of that shader and carried its warm-up. Warm, the two shapes are:

| | vertices | time | rate |
|---|---|---|---|
| instanced, 8 x 623k instances | 4.98M | 247 ms | **20 M/s** |
| flat, one glDrawArrays | 14.95M | 18 ms | **830 M/s** |

**Forty times the throughput per vertex, in the same app, the same frame, over
the same OpenGL ES.** That also answers the Metal question: 830 M vertices/sec
is the class Nomad works in (~5M polygons interactive on an iPad Pro), and we
reach it through GL ES three lines away in the same function. The API is not the
gate; the shape of the draw is. Metal stays a last resort, not a prerequisite.

The earlier "naive flat is a wash" note was computed from the 146 ms warm-up
number and is wrong.

### What it does

`glDrawElements` per batch of segments instead of `glDrawElementsInstanced` per
segment. Flat costs 24 vertices per box instead of 8 unless it is indexed, so it
is indexed: a static index buffer holds `segment * 8 + corner`, the shader
splits it with `gl_VertexID & 7` and `(gl_VertexID >> 3) + segment_base`, and
the post-transform cache still shades each of the eight corners once because the
values repeat inside a segment. The VAO for this path has **no vertex attributes
at all**.

Batched at 65 536 segments: a whole-model index buffer would be 96 bytes per
segment - 60 MB here - and would have to be rebuilt whenever the enabled set
changes. One static 6 MB buffer, ten draw calls, nothing per rebuild.

The one-shot benchmark is removed with this: it drew through the instanced path
with this shader and cannot survive the change. It has done its job - resolution
ruled out, shader ruled out (12%), instancing convicted (88%).

### Verified locally, before CI

`idxtest.cpp` in the scratch tree reproduces the index generation and the
shader's decode and checks they agree - which is the whole correctness argument:

```
index buffer 1572864 entries, 6.0 MB, 10 batches for 622855 segments
ALL CHECKS PASSED
```

* every index decodes back to its own segment and to a corner in 0..7;
* exactly **8 distinct corners** are referenced per segment, which is what makes
  the post-transform cache shade 8 instead of 24;
* `segment_base + local` gives the right global segment across every batch
  boundary, including the short last batch;
* the largest index value is the one the buffer is sized for.

Four builds were spent on mistakes CI could not catch and a local check could.
This is the pattern to keep: the arithmetic gets tested on this machine first.

## Run 159: flat drawing works — 241 ms to 6-13 ms

```
render +50  paths_ms 643  worst_ms  39   -> 12.9 ms a frame
render +12  paths_ms  68  worst_ms 102   ->  5.7 ms a frame
segs 1372026->622851   gpu 93 MB (was 88, = the 6 MB index buffer)
```

**Nineteen to forty-three times faster**, and the user reports it as smooth. The
whole four-round hunt lands here: the frame was never the shader, the model, the
resolution or the API. It was 623 000 instanced draws of eight vertices, and
`benchnull` said so the moment it was asked the right question.

Worth keeping as the lesson: `benchquarter` ruled out fragments, `benchnull`
ruled out the shader, `benchflat` convicted instancing. Three numbers, one build
each, after four builds of reasoning from frame times that could not distinguish
any of them.

## Two crashes, and only one of them is new

`orcacrash.txt` holds four reports. They are not the same bug.

**Three x SIGSEGV (pids 3418, 3729, 4081), identical stacks:**

```
objc_retainAutoreleaseReturnValue
UIKitCore ... _CFXNotificationPost
UIKitCore ... _UIScenePerformActionsWithLifecycleActionMask
```

Entirely inside UIKit's scene-lifecycle notification delivery, with no Orca
frame below the signal handler. This is the app being suspended or resumed, not
the renderer, and it predates this round. Untouched by anything here.

**One x std::bad_alloc (pid 4481), and this one is a real limit:**

```
load_print_as_fff: will load gcode_preview from result, moves count 5369727
std::bad_alloc exception: std::bad_alloc
```

**5.37 million moves**, against 2.4M for the model everything above was measured
on. It fails while building the preview, before any drawing. The previous model
reported `cpu 183 MB, gpu 93 MB`; this one is 2.2x the moves, and the load path
allocates several full-length arrays at once:

* `m_vertices` - 5.37M x sizeof(PathVertex), the largest single block;
* `positions` and `heights_widths_angles` in `extract_pos_and_or_hwa()`, two
  `std::vector<Vec4>` each reserved to the full vertex count - 86 MB apiece;
* `m_vertices_colors`, plus the colour copy `update_colors_texture()` makes;
* `enabled_segments`, plus `coalesce_collinear_runs()`'s return value reserved
  at twice its size, plus the copy `set_enabled_segments()` takes of it.

The peak is the sum of those, not the largest of them, and it is reached while
`m_vertices` is still alive. That is where to look: stream the Vec4 arrays per
chunk instead of building them whole, and move rather than copy the coalesced
list. Neither needs a device to reason about - the sizes are all
`vertices.size()` multiplied by a constant, so the arithmetic can be done here
first, the way the index-buffer test was.

Not a regression from 0410: its addition is a 6 MB static index buffer, which
the `gpu 93 MB` line accounts for exactly.

## 0411 — one array at a time on load

The `std::bad_alloc` from run 159, on a **5 369 727-move** G-code against the
2.4M one everything else was measured on. It fails while building the preview,
before anything is drawn.

`extract_pos_and_or_hwa()` filled two `std::vector<Vec4>` - a Vec4 per vertex,
**82 MB each at that size** - and they were built together, so the peak carried
both on top of `m_vertices`, which is the largest block in the program and is
still alive throughout. The function already supported building one at a time
(`update_heights_widths()` passes `nullptr` for positions), so the load path now
does the same: build positions, upload them, release them, then build the
heights/widths/angles.

**Splitting the walk is safe, and the reason is not obvious.**
`heights_widths_angles` carries an angle computed from `prev_line`, which
depends on `m_valid_lines_bitset[i - 1]`. The first pass updates bit `i - 1`
before it reaches `i`, so it is already reading the finalised value there - and
a second pass over the finalised bitset computes the identical angle. The bitset
must therefore be written by the first pass, which is why `true` stays on that
call and the second takes the default `false`. Get that order wrong and the
angles change silently.

`std::vector<Vec4>().swap(positions)` rather than `clear()`, because `clear()`
keeps the capacity, which is the whole 82 MB.

Second, smaller one: `update_enabled_entities()` held three model-sized arrays
across one call - `enabled_segments`, the coalesced runs built from it, and the
copy `set_enabled_segments()` takes. It takes a `const&` and copies into its own
textures, so the runs cannot be moved into it, but the list they were built from
can be dropped first. Three become two.

**Peak on that model: about 102 MB lower.** Whether that is enough for 5.37M
moves is not established - `m_vertices` alone is far larger, and this only
removes what was redundant. If it still throws, the next candidates are
`m_vertices_colors` plus the copy `update_colors_texture()` makes, and then
`m_vertices` itself, which would need chunked upload rather than a resident
copy.

### The VBO is not in this build, deliberately

It bakes ~100 MB of box geometry at load, on the model that is already dying of
memory at load. Doing both at once would have shipped a fix and its own
counterweight in the same binary, and left no way to tell which number moved.
The order is: land the memory work, confirm the 5.37M model opens, then bake.

Also worth weighing before building it: the frame is now **13.5 ms**, of which
paths is ~12.5 ms, and the flat draw already removed the cost that dominated.
The VBO's remaining prize is the six texture fetches per vertex - real, but this
is no longer a 40x lever, and it costs the memory headroom this patch just
bought.

### 0415 — the page border, dimmed tools, and their names

**The grey band was never the strip and never the sizer.** Two rounds went on
`m_bookctrl->Hide()` and then on removing the sizer item, and it was still
there, because `wxBookCtrlBase::GetPageRect()` computes

    rectPage.y = size.y + GetInternalBorder();

With the controller hidden `size.y` is zero, so the **internal border is the
entire offset** — a band of the notebook's own grey across the top of every
page, owed to neither of the things that were removed. It defaults to 5 and
nothing on this port wants it: `SetInternalBorder(0)`.

Worth keeping as a method: when two plausible fixes both fail, the cause is
usually a third thing that neither touched. Reading `GetPageRect()` would have
answered it before either attempt.

**A disabled tool did not look disabled.** UIKit dims a disabled button's
*title*, not its image — and these buttons carry `AlwaysOriginal` images, which
is what keeps Orca's own icon colours. So the whole button is dimmed instead
(`alpha 0.35`), which is the clearer signal anyway.

**And a long hover names the tool.** `UIToolTipInteraction` is what iPadOS
shows for a pointer resting on a control, and the text is the tooltip Orca
already wrote for the item — "Add [Ctrl+I]", "Auto orient all/selected objects
[Q]" — shortcut included, at no cost, because it was already being carried
across for the accessibility label.

**German, second attempt.** Naming the language was not enough: the interface
still came up English. wxLocale cannot set a locale on this device — the log
says so every launch — and Orca's path abandons the translation along with it.
`wxTranslations` needs no C locale, so the catalog is now loaded directly and
whatever Orca manages afterwards is a bonus rather than the only chance. The
line `orca-ios-lang: de catalog from <dir> -> loaded|NOT FOUND` says whether
the .mo is even in the bundle, which is the other thing that could be wrong.

### 0416 — the legend clears the Slice capsule

The totals, the cost and the time estimate are `GCodeViewer`'s legend, pinned to
the **top right of the canvas** — the same corner the Slice and Print capsule
floats in, so the capsule was drawn over it.

It cannot be reordered against the capsule: the legend is ImGui *inside* the GL
canvas, and the capsule is a UIKit view above the canvas. There is no z-order
between them to fix. So the legend starts lower instead, by
`orca_ios_tab_bar_inset()` — the same number the Device page uses, so the two
stay in step if it is ever tuned. The g-code viewer window in the same corner
moves with it.

`GCodeViewer.cpp` had no `TargetConditionals.h`; without it `TARGET_OS_OSX`
reads as zero and the offset would compile into the macOS build. That is the
third header in this stack with the same gap — Notebook.hpp (0372), Tabbook.hpp
(0413), and now this one. **Check it in every file this stack reaches into.**

## 0417-0421 + 0229-0230 — the preview tab, and dialogs that are windows

Seven reports from the device, one build. Two of them turned out to be the
same class of bug in two different recognizers, and one of them was a fix from
an earlier round that had aged out of its own reasoning.

### The pan jump at the end of every orbit is 0227, again

**`WX_trackpadPanGesture:` had the guard without the ordering.** 0228 added a
second pan recognizer so that two fingers on a *trackpad* slide the plate
instead of zooming it, and guarded it with `numberOfTouches > 0` — the same
guard `WX_scrollGesture:` uses to keep a finger drag out of the wheel path.
0227 had already found what is wrong with that guard used alone:

> by the time UIKit reports Ended the count has ALREADY dropped to zero, so the
> guard stops protecting at exactly the wrong moment.

A one-finger drag drives that recognizer too — `allowedScrollTypesMask` decides
which *indirect* scrolls it accepts, it does not stop a direct touch from
panning it. Began and Changed are rejected, the finger lifts, Ended arrives
with a touch count of zero, and the whole drag's translation is delivered as a
single pan delta. That is the plate jumping every time a finger is lifted off
an orbit, and it is the same shape as the zoom jump 0227 fixed.

`0229` drops Ended and Cancelled outright, which is what 0227 did. An indirect
pan still starts from zero, because `wxOSX_panGesture` resets its cumulative
origin on Began and Began still gets through.

**Worth keeping as a rule: a guard copied without its ordering is not the same
guard.** Both recognizers now read the same way, three lines apart.

### The slider orbited the model because ImGui answers about last frame

`ImGuiWrapper::update_mouse_data()` returns `io.WantCaptureMouse`, which
`NewFrame()` computed from where the pointer was **then**. With a mouse that is
harmless — the cursor has hovered the widget for at least one frame before the
button goes down. A finger arrives with the button already down at a point
ImGui has never been told about, so the touch is judged against wherever the
last one ended: the press falls through to the camera, the *next* frame notices
the slider under the finger, and from there both are being dragged at once.

ImGui documents the answer in `imgui_internal.h`:
`UpdateHoveredWindowAndCaptureFlags()` is exposed "on touch-based system that
don't have hovering, we want to dispatch inputs to the right target (imgui vs
imgui+app)". `0417` re-runs it on every button-down with the new position and
no button yet held — which is the state the event is in, because
`update_mouse_data()` is what sets `MouseDown` — and `WantCaptureMouse` then
describes where the finger actually landed.

The second half is the early return underneath it. Upstream keeps two
exceptions to "ImGui took it, stop here": a non-empty tooltip and the
multi-material gizmo following the cursor. Both are about a pointer *hovering*,
neither can happen with a finger, and both hand the event to the camera as a
side effect — the first one through sets `m_mouse.dragging` and the drag
origin, and every step after it orbits. On iOS the rule is now the simple one:
if ImGui wants it, ImGui gets it, unless the camera was already mid-drag when
the finger crossed onto an ImGui window.

**Both halves are needed.** The first stops the press reaching the camera; the
second stops the drag steps reaching it.

### The toolpaths follow the slider now (0390 is retired)

0390 deferred the rebuild until the slider had been still for 250 ms, because
handing a new range to libvgcode measured at ~775 ms. **Most of that number was
the draw.** 0410 replaced 623 000 instanced draws with one flat indexed draw —
241 ms to 6-13 ms — and 0411 took the load peak down beside it. What is left in
a drag step is libvgcode's own walk to rebuild the enabled-segment set, which
the frame report already times as `vg_enabled_ms`.

`0418` reads that counter either side of `GCodeViewer::render()`, which is the
measurement with no new timing code, and applies the slider value once at least
as long has passed since the last apply as the last rebuild took (capped at 300
ms). On a model where a rebuild is 10 ms that is every frame — live. On a very
large one it is three times a second, and the drag still draws at the frame
rate in between, because only the rebuild is spaced out. The value the slider
stops on is never lost: it stays dirty until it is applied.

### The toolbar was over the slider, and Preview was stealing it

Two separate things behind one report.

**The column is a singleton, and every canvas commits into it.** Prepare,
Preview and the assemble view each sync from their own `on_idle`, so the last
one to run took the toolbar away from the others — and Preview's set is one
item (the sidebar toggle; its main toolbar and gizmos are not enabled) against
Prepare's twenty-five. `0419` ignores a commit from a canvas that is not on
screen, which is a three-line walk up the superview chain.

**And it now clears the slider.** The vertical layer slider is pinned to the
right of the canvas by `IMSlider::render()`; it is ImGui *inside* the GL scene
and the toolbar is a UIKit view *above* that scene, so there is no z-order
between them — the same shape as the legend problem in 0416. The canvas passes
the width it has already spoken for on that edge, in canvas pixels, and the
column moves in by it, converted to points with the host view's
`contentScaleFactor`.

**By the track, not by the window.** The first cut of this moved the column in
by the slider's whole window — 164 units, about a hundred points — and the
device answered "the toolbar is too far left", which it was: the window is that
wide because the label boxes hang off to the *left* of the groove, and the
groove and its handle live in the last forty of it. `IMSlider` now exposes that
forty (`vertical_slider_track_width()`, built from the same terms
`vertical_slider()` derives `groove_start` from, so the two cannot drift), and
the column clears the track with a gap rather than the window. The labels can
pass under it; the drag can always be reached, which is the part that matters.

### The plate pictures in Preview were never rendered

`PartPlate::thumbnail_data` is filled by `Plater::update_all_plate_thumbnails()`
— and the call that was meant to produce them for that column is in
`GLCanvas3D::on_set_focus()`, when the preview canvas takes the keyboard focus.
**There is no focus to take on this port.** `wxWidgetIPhoneImpl::SetFocus()` is
a stub that returns `false` and no UIView sends `wxEVT_SET_FOCUS`, so the
canvas is never told and the column comes up as plate numbers over empty
buttons.

The second half: the textures are built exactly once.
`_update_imgui_select_plate_toolbar()` returns immediately once
`is_render_finish` is set, so a thumbnail that becomes valid *later* — after a
save, after a slice — is never picked up either.

`0420` answers both by looking at what the column is actually holding rather
than by hooking an event: if a plate has no thumbnail or an item has no
texture, render what is missing and clear the flag. Rate limited to one attempt
per two seconds, because a miss is a full offscreen pass per plate.

### Textured PEI is the plate

Two fallbacks, because the bed type is read from two places. `AppConfig` seeds
`curr_bed_type` with `"1"` (btPC, Cool Plate) on a fresh install, and
`Preset::get_default_bed_type()` answers for a printer whose bed has never been
chosen — which is every printer until someone picks a plate in the sidebar.
`0421` sets both to `btPTE` on iOS. An explicit choice still wins: it is stored
per printer and read before the fallback is reached.

`Preset.cpp` had no `TargetConditionals.h`. That is the fourth file in this
stack with the same gap — see 0416 — and the check is now part of writing any
patch that tests `TARGET_OS_OSX`.

### 0230 — a dialog is a window

`wxCAPTION | wxCLOSE_BOX`, or `wxDEFAULT_DIALOG_STYLE` which contains both, is
how every dialog in this application asks for a title bar with an X in it. On
Windows and macOS the window manager draws that bar. On iOS there is no window
manager: `wxNonOwnedWindowIPhoneImpl::Create()` reads `wxCAPTION` into an empty
branch, so a dialog arrived as a bare rectangle of controls — no title, no
edge, no way to dismiss it. Two open at once were indistinguishable from one,
and a dialog clamped to exactly the screen was indistinguishable from the
application behind it. That is the "borders run into the other UI".

So the port draws the bar: one `UIView` per decorated window above the window's
root view, carrying the title and a close button on the iOS 26 glass material,
with the window inset from the screen edges, its corners rounded, and a dim
backdrop between it and everything below.

**The header is not client area**, and that is the whole of how it stays out of
the dialog's way. `GetContentArea()` hands its height back, so
`GetClientAreaOrigin()` offsets every child the sizer places, `DoGetClientSize()`
reports the room that is left, and `Fit()` / `SetSizerAndFit()` — which both go
through `GetSize() - GetClientSize()` — grow the window rather than losing the
bottom row of buttons behind the bar. It is exactly how a native title bar is
accounted for on the desktop ports.

**The X sends `wxEVT_CLOSE_WINDOW`**, which is what a desktop title bar's X
sends: `wxDialogBase::OnCloseWindow` turns it into `EndDialog(wxID_CANCEL)` and
an application may veto it. It is dispatched asynchronously and disarms itself
on the tap, because closing a modal dialog unwinds the modal loop and can
release the button's own view while it is still inside its action method.

**The backdrop never takes a touch.** Swallowing them would be the correct modal
behaviour and the wrong risk here: a window left up by mistake — and this port
has had several — would then take the whole application with it rather than
merely dimming it.

**`UIGlassEffect` is compiled out, not `@available`-guarded.** It is a class:
on an SDK that has never heard of it the availability check does not help, the
name does not resolve, and the build fails. wx is built by more than one
workflow here and `ios-step2-wxwidgets` / `ios-wx-only` still build at
`IOS_MIN=17.0` on purpose. `__IPHONE_OS_VERSION_MAX_ALLOWED >= 260000` decides
whether the glass path exists at all, and a system material stands in below it.

### 0422 — the summary was half a screen's worth of points too high

`orca_ios_tab_bar_inset()` is UIKit: it returns the height of a view, in
**points**. 0416 added it to an ImGui window position, which is in canvas
**pixels** — right next to a `4.0f * m_scale` that does the conversion
properly. On a 2x screen the legend therefore cleared 36 points where 72 were
asked for and came up half under the Slice/Print capsule. Multiplying by
`m_scale` is the whole fix, in the legend and in the debug window beside it.

Worth keeping as a rule for this port: **anything that crosses between UIKit
and ImGui changes units.** The toolbar inset in 0419 goes the other way and
divides by `contentScaleFactor` for the same reason.

### 0423 — the grey bar above the sidebar is an AUI caption

Reported from the device as "a grey bar at the top left", and it is eighteen
pixels of empty grey across the top of the sidebar, above "Printer".

It is a **wxAUI pane caption**. Orca makes the sidebar a docked AUI pane
("Orca: Make sidebar dockable"), and `wxAuiPaneInfo`'s default constructor runs
`DefaultPane()`, which sets `optionCaption` along with the dockable and
floatable flags. Nothing ever sets a caption *string*, so the pane draws a bar
with nothing in it, whose only function is to be dragged — tear the sidebar off
into a floating window, or re-dock it on the other side.

There is nowhere to drag it to on this port: the window is the screen.

**Through the art metric, not through `CaptionVisible(false)`.** The pane's
flags are written into the saved perspective (`app_config` "window_layout") and
restored by `LoadPerspective()` forty lines further down, so a flag cleared at
`AddPane()` comes straight back on any device that has run the application
before — which is every device. `wxAUI_DOCKART_CAPTION_SIZE` is layout art, not
part of the perspective, and a caption of zero height cannot be laid out, drawn
or hit-tested whatever the flags say.

### Not verified on device yet

All of the above is reasoned from the sources and from the counters the earlier
rounds installed; none of it has been through a device build at the time of
writing. The two with the most surface are the dialog chrome (every wxDialog in
the application changes size by 52 points) and the plate-thumbnail pass (an
offscreen render started from an idle). If a dialog comes up with its bottom
row clipped, `GetContentArea()` is the one place to look.

## 0424 — every way the work was lost, and the one hook that was missing

A full walk of saving, loading and autosaving, end to end, because the reports
kept coming back to the same shape: work that was on the screen and then was
not. Seven separate faults, of which one is the whole platform and six are
consequences of not having noticed it.

**The platform fault: there is no quit, and nothing was listening for what
replaces it.** On a desktop an application is closed by the user and runs
whatever it likes on the way out. iOS does none of that. Leaving the app
*suspends* the process - frozen mid-instruction - and the system reclaims it
later, minutes or days on, without running another line of its code. Before
this patch the port hooked none of that: `grep` for `didEnterBackground`,
`willResignActive` or `willTerminate` across `patches/`, `wx-overlay` and
`orca-overlay` returned nothing at all. Everything that had not reached disk by
the last ten-second backup tick was gone, and there was no code path that could
have known.

wx does implement `applicationWillTerminate:`, and it is not the hook. UIKit
sends that only to an application that is *still running* when it is killed,
which a suspended one by definition is not.

`UIApplicationWillResignActiveNotification` is the one. It arrives while the
application is still frontmost and fully alive - the user has started the
gesture, or pulled a banner down, or taken a call - and it precedes every route
to suspension there is.

### The three notifications are not interchangeable

| notification | state | what is written |
|---|---|---|
| `willResignActive` | foreground, rendering legal | everything: backup 3MF, the copy in Documents, the config |
| `didEnterBackground` | GL is now grounds for termination by the OS | backup 3MF and config only |
| `didReceiveMemoryWarning` | foreground, but jetsam is circling | backup 3MF and config only |

The split is the `full` argument of `orca_ios_save_everything_now()`. A 3MF
export that is not a `Backup` re-renders every plate thumbnail - four offscreen
GL passes per plate - and that is exactly what must not happen in the
background, and exactly the allocation a memory warning is warning about. Orca's
own backup 3MF touches no GL and no thumbnails, so it is written first in every
case; it is also the one the restore offer on the next launch reads, which makes
it, not the copy in Documents, the thing standing between a suspend and lost
work.

### The rate limit was dropping edits, not deferring them

0366 rate-limited the copy into `Documents/Autosave` to once a minute, because a
full export regenerates thumbnails and the backup tick is every ten seconds.
That was right. What was wrong is what the caller did next:

```cpp
if (!up_to_date(false, true)) {
    export_3mf(backup_path + "/.3mf", SaveStrategy::Backup);
    orca_ios_autosave_to_documents(m_plater);   // may return immediately
    up_to_date(true, true);                     // ... and the model is clean anyway
}
```

The skipped write was never retried. `up_to_date(true, true)` marked the model
as backed up, the next tick saw no change and did not call the function at all,
and an edit made inside the sixty-second shadow of the previous autosave did not
reach `Documents` late — **it never reached it**. The copy was as much as a
minute stale and nothing said so.

The flag now outlives the skipped write. `orca_ios_autosave_note_change()` sets
it, `orca_ios_autosave_to_documents(plater, force)` clears it only on a
successful export, and the call is made on *every* tick rather than only on
ticks where the model moved - so a deferred copy is retried ten seconds later
instead of being forgotten. The limit still decides *when*; it can no longer
decide *whether*.

### Crash recovery had never once worked on this port

`has_restore_data()` is what puts up "Previously unsaved items have been
detected. Do you want to restore them?". It refuses when the backup directory
looks locked by a live process, and it decides that by resolving a pid to an
executable name:

```cpp
if (get_process_name(boost::lexical_cast<int>(pid)) == get_process_name(0))
```

iOS cannot resolve a pid that is not our own - the sandbox forbids inspecting
another process - so `get_process_name()` there ignores its argument and returns
our own path from `_NSGetExecutablePath`. **Both sides are the same string for
every pid that has ever existed.** Every lock read as ours, the function always
returned `false` with `origin = "<lock>"`, and the offer could not appear. The
same `<lock>` answer also stopped the caller ever deleting a stale backup
directory, so they accumulated.

The question is answerable exactly on iOS without asking about other processes
at all: the system runs one instance of an application at a time, so a lock is
live if and only if it is ours. `pid == get_current_pid()`, and nothing else.

**And the thing being restored was in `tmp`.** `Model::get_backup_path()` builds
on `temporary_dir()`, which is `wxFileName::GetTempDir()`, which on iOS is the
container's `tmp` - purged whenever the system wants the space. That is the same
root cause 0382 found under projects that "could not be opened", one directory
over. The backup now lives under `data_dir()` (`Library/Application Support`),
which is not purged and not shown in the Files app - right for a working
directory. The copy in `Documents/Autosave` is the one meant to be found by
hand.

Consequence worth expecting on device: **the restore prompt now appears on any
launch that follows a session which ended without an explicit save.** That is
not a regression, it is the feature - there genuinely were unsaved items, and
this is the only route back to them from inside the app.

### The recent list was written to disk by accident, when at all

`AppConfig::set_recent_projects()` writes straight into `m_storage` instead of
going through `set()`, so it never marked the config dirty - and `GUI_App`'s
idle handler only saves when `app_config->dirty()`. The list therefore survived
only when some unrelated setting happened to change in the same session. On a
platform that ends sessions by killing the process, that is most of them: the
autosave file was written and the entry pointing at it was not. One line,
`m_dirty = true`, and it is a genuine upstream bug rather than an iOS one.

### The container UUID was healed for display but never in the history

0382 splices the current container UUID into recorded paths at two places -
`get_recent_projects()` and `open_recent_project()` - and both are *downstream*
of `m_recent_projects`. What wxFileHistory held stayed stale, and two reported
symptoms follow directly:

* `LoadThumbnails()` reads each 3MF at the path **in the history**. Stale path,
  no file, no thumbnail - the home page comes up as a column of grey tiles after
  every install.
* Opening one of them goes on to `set_project_filename()`, which calls
  `add_to_recent_projects()` with the **healed** path. `wxFileHistory` dedupes on
  the exact string, so the healed path is added *beside* the stale one and the
  project is on the home page twice.

Healing now happens on the way in, in `init_menubar_as_editor()`, into the
history itself; entries that cannot be healed and do not exist are dropped
rather than kept as unopenable rows; and the healed list is written back so the
next launch starts from good paths. The two downstream calls stay where they
are, as the backstop they were meant to be.

The same splice is now applied to the **origin file** of a restored backup.
That path was written by the run that died, so it names that install's
container; restoring left the project pointing at a directory that no longer
exists, and the next Save reported "Failed to save the project."

### And the autosave copy was the other half of the duplicate

0387 stopped a MakerWorld download being listed as a project. The same shape was
still live one directory over: a project opened from `Documents/Projects/X.3mf`
is on the home page under that path, and 0369 added `Documents/Autosave/X.3mf`
beside it - two tiles, same name, no way to tell them apart. The copy is still
written; it just does not get a second tile when the project already has a file
of its own. A project that has *never* been saved still gets its entry, because
for that one the copy is the only way back.

### The switch that turned the whole safety net off

`backup_switch` is a desktop convenience, and on iOS it was the master switch for
everything above - the copy in Documents, the restore offer, the save on leaving
the foreground, all of it hangs off that one timer. Setting the interval to zero
did not save a resource, it discarded the work.

The timer is now unconditional on iOS, in `MainFrame`'s constructor and at all
three places in Preferences that could reach zero (the switch, an empty interval
field, a typed zero). The checkbox is shown ticked and disabled with a tooltip
that says why, rather than removed: a control that does not do what it says is
worse than one that is plainly unavailable. How often is still the user's
choice; whether is not.

Two crashes fixed in passing, both `boost::lexical_cast<long>` on strings that
come from an editable field: one in the `MainFrame` constructor (a throw there
is a launch that never finishes) and one in the Preferences OK handler (a dialog
that cannot be closed).

### Save had no button

Reachable saves before this patch: `Cmd+S`, and File > Save Project in the
iPadOS menu bar. Both need a hardware keyboard. The home page's `+` offers New
and Open only, and the native toolbar column mirrors the GL toolbars, which have
no save on any platform. **An iPad used with a finger could not save its work at
all.**

`app:save` is now the first item in the toolbar column on the 3D view, using
Orca's own `menu_save.svg` and enabled from `MainFrame::can_save()` - the same
test that greys the menu row out. The tap defers through `CallAfter` because
`save_project()` can put up a picker or an error, and this runs inside a
`UIButton` action on a view the idle sync owns; 0230 is what a modal loop does
to the view underneath it. Save As stays in the menu: there is one action on the
column and it is the one that does not ask anything.

### Where a project lives now, in one place

| path | written by | on the home page |
|---|---|---|
| `Documents/<name>.3mf` | Save, on a project with no file yet (0367) | yes |
| `Documents/Projects/` | Open and Import, moved out of `tmp/*-Inbox` (0382) | yes |
| `Documents/Downloads/` | MakerWorld deep links (0377) | no, by 0387 |
| `Documents/Autosave/<name>.3mf` | the autosave copy, ≤ 60 s stale, forced on resign-active | only if the project has no file of its own |
| `Library/Application Support/OrcaSlicer/backup/…/.3mf` | every backup tick with a change, and every lifecycle hook | no - it is what the restore offer reads |

### Not verified on device

None of this has been through a device build at the time of writing. The two
with real surface are the lifecycle hooks - if the app is killed while
backgrounding, `willResignActive` doing a full export on a very large model is
the first thing to suspect, and the backup written before it is the part that
must not be moved - and the toolbar's new item, which is the first entry in that
column that does not come from a `GLToolbar`.

Two things left open on purpose:

* **`Documents/Autosave` is never pruned.** One file per project name, kept for
  ever. Deleting files out of a directory the user can see in the Files app is
  not something to do without asking, so it is not done.
* **pid reuse.** The iOS lock check compares against `get_current_pid()`, and
  pids wrap. A collision means one missed restore offer, never a lost file -
  the safe direction - but it is why the check is not exact.
### 0417 and the catalog that was never built — German, third attempt

The log line 0415 added would have said `NOT FOUND`, because **no `.mo` was
ever produced anywhere in this repository.**

Orca keeps its translations as `localization/i18n/<lang>/OrcaSlicer_<lang>.po`
and compiles them to `resources/i18n/<lang>/OrcaSlicer.mo` through the
`gettext_po_to_mo` CMake target. That target is `add_custom_target` without
`ALL`, so it is not part of `all`, and every workflow here builds exactly
`libslic3r_gui` and `OrcaSlicer` — never it. Upstream's `resources/i18n/`
holds one file, `placeholder.txt`, and that is precisely what
`cp -R orca/resources "$APP/orca-resources"` has been shipping. So
`localization_dir()` in the bundle was an empty directory, `AddCatalog()`
had nothing to find, and the app was English however firmly 0412 and 0415
asked for German. Nothing about wxLocale was ever the reason.

**The catalog is now committed, compiled, in the overlay.**
`orca-overlay/resources/i18n/{de,de_DE}/OrcaSlicer.mo` is copied into the Orca
tree by the `cp -R orca-overlay/. .` every step-3/step-4 workflow already runs,
and from there into the bundle. Both directory names are shipped because 0415
loads the catalog itself with `SetLanguage("de")` while Orca's ordinary path
asks for the config value `de_DE`; whichever runs first, it is where it looks.
No gettext on the runner, no CMake target, nothing to remember in CI.

`tools/i18n/build-de-catalog.py` compiles the `.po` (pure Python — the MO
format is a header, two sorted string tables and an index; verified byte-for-
byte against GNU `msgfmt` after `msgunfmt`). **Run it after editing the `.po`.**
`--check` recompiles to memory and compares, and the step-3/step-4 workflows
run it right after the overlay copy, so a `.po` edited without rebuilding the
`.mo` fails the build instead of silently shipping the old strings. Step 4 also
asserts both catalogs are in the payload after `orca-resources` is copied.

**The translation itself.** The source is now
`orca-overlay/localization/i18n/de/OrcaSlicer_de.po`, which overrides upstream's
on the same overlay copy. Upstream's was 5506 of 5551 messages and rough in
places; it is now 5553 of 5553, and the pass over it fixed, among others:

- 45 untranslated messages, including the whole chamber-temperature group.
- Sentences that stopped in the middle — "…ist mit den gewählten Druckerprofilen
  nicht", "…um den Filamenttyp" — and one message with a second message glued
  onto its end (`Project downloaded %d%%`).
- Labels that lost the `: ` or the trailing space the value is appended to, so
  the calibration dialogs read "Starttemperatur220" (51 of them).
- Translations that said something else than the English: `Face recognition` →
  "Gesichtserkennung", `This is the number of top interface layers.` → "Anzahl
  der langsamen Schichten", `Maximum acceleration for travel` → "Maximale
  Fahrgeschwindigkeit", `Boldness` → "Durchschlag", `Open Preferences` →
  "Einstellungen.", `Undo translation` → "Übersetzung rückgängig machen".
- `%1%` invented for a msgid that has no placeholder, printed literally.
- One term per concept: Skirt is "Schürze" and no longer collides with Brim's
  "Umrandung"; Travel is "Verfahrweg"; Preset is "Profil"; Modifier is
  "Modifikator"; flushing is "spülen"; plug-in is "Plug-in".
- The informal "du" in a catalog that is otherwise "Sie", and the "wählen sie"
  that should be "wählen Sie".

**0417 — the labels this file writes itself.** Everything the native UI shows
arrives already translated from the C++ side, because `_L()` is not reachable
from `ios_webview_support.mm`. Three labels were its own and stayed English.
`_L(s)` is only `wxGetTranslation(wxString(s, wxConvUTF8))`, which *is*
reachable, so they go through the same catalog now; their msgids are carried in
our `.po` (an msgid the catalog does not have comes back unchanged, which is
the English, so this is safe for the other languages).

### 0425/0426 — the last English in a German app

Two more places the catalog does not reach, found by walking the whole
interface rather than the .po.

**0425 — the labels `ios_webview_support.mm` writes itself.** Everything else
the native UI shows arrives already translated from the C++ side (tab titles,
tooltips, the two entries under the plus) because `_L()` is not reachable from
that file. Three labels were its own and stayed English: the plus button's
accessibility label and the tool column's chevron ("More tools" / "Fewer
tools"). `_L(s)` is only `wxGetTranslation(wxString(s, wxConvUTF8))` — see
`I18N.hpp` — and *that* is reachable, so they go through the same catalog now.
Their msgids are carried in our `.po`; an msgid the catalog does not have comes
back unchanged, so this is safe for every other language.

**0426 — the web pages have a second translation table, and German had holes.**
The home page and the whole setup wizard are HTML under `resources/web/`, and
they do not use gettext at all: `TranslatePage()` in `resources/web/data/text.js`
swaps every `.trans` node against `LangText[<lang>]`, where `<lang>` is the
`?lang=` the C++ side appends — `current_language_code_safe()`, which maps
`de` to **`de_DE`**, the key the table uses. A tid missing from that block
leaves the English in the page.

78 tids are used by the pages. Seven were missing from `de_DE` (`orca3`,
`orca4`, `orca5`, `orca12` — the Stealth Mode page of the wizard; `t109`,
`t110`, `t111` — the filament page) and four more were still English
(`orca1`, `orca2`, `orca7`, `orca8`, plus the two plug-in paragraphs `t65`
and `t75`). All filled. What is left identical is "Bambu Cloud" and "China",
which are names.

**Not touched: `_L("Get Started")` in MainFrame.cpp (0340).** It is a lookup
key for the simulator drive harness (`ORCA_IOS_DRIVE`, never set on a device),
and the button it looks for is an HTML anchor in the wizard whose text comes
from `text.js` (`t24`, now "Loslegen"). "Get Started" is not in Orca's `.pot`
either, so `_L()` returns it unchanged — adding it to our catalog would change
a search string and fix nothing.

**Where the German comes from, in one place:**

| surface | mechanism | source |
|---|---|---|
| wx UI, menus, parameters, dialogs | gettext | `orca-overlay/localization/i18n/de/…po` → `resources/i18n/{de,de_DE}/OrcaSlicer.mo` |
| home page, setup wizard | `LangText` in JS | `resources/web/data/text.js`, key `de_DE` (patch 0426) |
| the native tab bar, toolbar, capsule | strings passed in from C++ | already translated by the first row |
| three labels written in ObjC++ | `wxGetTranslation` | first row, via patch 0425 |

### 0427 — one palette, and the plist that made dark mode a no-op

**The device's dark mode did nothing, and the reason was not in the code.**
`Info.plist` carried `UIUserInterfaceStyle = Light`, written by the step-4
workflow. That key does not tint anything - it tells UIKit the app has one
appearance, so every trait collection in the process reports light, and with it
`orca_ios_system_is_dark()`, `wxSystemAppearance::IsDark()`,
`GUI_App::dark_mode()` and every trait-change callback. 0414 taught the
application to read the system honestly and 0427's first version wired the
switch through to every last copy of the flag; both were correct and neither
could ever fire. The key is gone. It was added for a real reason - Orca's
panels were hardcoded white while its text followed the system, so after sunset
the "Printer", "Filament" and "Process" headers went light-on-white - and that
reason is what the rest of this patch removes.

**Orca has no palette, and now it has one.** Its colours are literal hex in the
source, forty-odd of them, with a table in `StateColor.cpp` mapping each to
something darker for dark mode - pairs with no scale behind them, and nothing
at all for light mode, where every colour is used exactly as written. In place
of that:

| tone | light | dark | what it is |
|---|---|---|---|
| page | `#FAF9F5` | `#262624` | the window, the 3D view, every web page |
| surface | `#FFFEFB` | `#30302E` | cards, inputs, popups, panel headers |
| sunken | `#F2F0E9` | `#1C1B1A` | side strips, top bar |
| fill / hover | `#EDEAE0` / `#E4E0D3` | `#3A3A37` / `#45443F` | button faces, tracks, row highlight |
| knob | `#FFFDF7` | `#C9C6BE` | the moving part of a switch |
| border / strong | `#E2DFD5` / `#D2CEC1` | `#3E3E3B` / `#514F4A` | separators; input and box outlines |
| text / soft / muted / dim | `#1F1E1D` `#3F3E3A` `#6F6E69` `#A3A099` | `#F5F4EF` `#E5E3DC` `#B4B2AA` `#7C7A74` | four weights |
| accent (+hover, tint, soft) | `#009688` | `#2CA898` | ORCA turquoise, and it is the only accent |

Warm neutrals, because a grey scale with no temperature next to a turquoise
reads as cold rather than neutral. Two lookup tables are built from one list of
"this colour Orca writes means this tone", so **light mode is a design now
rather than the absence of one** - the reason the first attempt left white
patches everywhere was that it translated one colour and left the other
thirty-three as Orca wrote them.

**What is deliberately not in the scale:** the blue, the orange and the red.
Those say something - a link, a warning, an error - and keep Orca's own values
including its dark variants. Four unused BBS greens are carried the same way so
that anything still reaching for one behaves exactly as before.

**Round trips, which is what the table has to get right.** `UpdateDarkUI()`
reads a window's *current* background back and asks again, so every tone has to
answer for its own colour in either appearance, not just for Orca's. Both
tables therefore contain each tone's light and dark value as keys as well.
Orca's own colours are inserted last and win: `#FFFFFF` is the window
background before it is anything else, which is also why the surface tone's
light value is `#FFFEFB` and not pure white - two meanings for one colour is
the one thing a lookup table cannot express. `tools/…` has no test for this;
the check that was run is generated from the source block itself and lives in
the commit message.

**The nine colours that never reach StateColor** - `m_color_label_default`,
`m_color_highlight_default`, `m_color_selected_btn_bg` and the rest - are read
straight off `GUI_App`, so `init_label_colours()` now asks the palette for them
with the colour Orca means. `SetDarkMode()` moves to the top of that function
because it is what the lookups answer against.

**The native chrome had a colour from nowhere.** The tab bar, the toolbar, the
slice/print pair and the numeric keypad were taking UIKit's system blue.
They take `orca_ios_accent()` now - a dynamic `UIColor` carrying the same two
turquoises as the accent tone, so UIKit resolves it per appearance and a switch
needs nothing re-set. One `self.view.tintColor` on the tab bar controller
covers the selected tab and everything added under it.

**The web pages carry the scale as literal hex**, because a stylesheet cannot
call C++: `global.css`'s token block for both appearances, and each page's own
`body`. They were translated mechanically from the same table - a light
stylesheet writes Orca's own colours, a dark one writes what Orca's dark table
produced, so the two directions needed two mappings. Six tokens whose Orca
value crossed two tones (`--bg-color-alt`, `--icon-color`, and the dark
`--bg-color-secondary`) are set outright.

**The white boxes, from the device.** Reported against the first build that
could actually show dark mode: the sidebar's "Printer", "Filament" and
"Process" titles sat in white boxes. `GUI_App::UpdateDarkUI()` was translating
a colour no window had chosen. `GetBackgroundColour()` answers for a window
that was never given one - it returns the system's, and on iOS that is
`UIColor.systemBackground`: pure black in dark, pure white in light. The
palette reads `#000000` as text, because that is what black means everywhere
in Orca's source, so a `Label` nobody had ever coloured came back near-white -
and setting the answer turned a `UIView` that was transparent
(`backgroundColor` nil, showing whatever it sits on) into an opaque one. Two
wrongs, one line apart.

`UseBackgroundColour()` and `UseForegroundColour()` say whether the window
chose, and all four assignments in that function are gated on them now. Left
alone the labels stay transparent and show the panel they sit on, which is
what they did before anything translated them. Off iOS both flags are
hardcoded true, so nothing changes there. Worth remembering as a class of bug
rather than one instance: any colour read *back* out of a window may be the
system's, and the palette cannot tell that apart from a colour Orca meant.

**The white boxes, second attempt — and why the first one missed.** The first
fix gated `UpdateDarkUI()` on `UseBackgroundColour()`, on the theory that the
labels had never chosen a background. They had: `Label`'s constructor calls
`SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent))`, so the
colour is *stored at construction* and kept for life. That changes the
question from "what does the walk do" to "what was true when the window was
built", and three things could be wrong there. All three are fixed:

* **`currentTraitCollection` is not an answer during startup.** It is only
  meaningful inside a UIKit callback that sets it - `traitCollectionDidChange:`,
  `layoutSubviews`, `drawRect:` - and outside one it can report Unspecified,
  which is not Dark. `init_label_colours()` builds the whole palette from it
  while the main frame is still being constructed. `orca_ios_system_is_dark()`
  now asks a `UIWindowScene`, which knows at any time, and falls back to
  `currentTraitCollection`.
* **`GetParentBackgroundColor()` handed out the system's colour.** Its
  fallback is `parent->GetBackgroundColour()`, which answers with
  `UIColor.systemBackground` for any parent that never chose one - black in
  dark, white in light. The palette reads `#000000` as text, so a label that
  stored it came back near-white the next time anything translated it. It now
  walks up to an ancestor that actually chose, and answers with the page tone
  if none did.
* **The interface was never walked once it was up.** `applied` was seeded from
  the config, so a device that started dark and stayed dark never triggered
  the walk that repairs a stored colour. It is no longer seeded, and is not
  latched until a walk has actually happened; `viewDidAppear:` on the tab bar
  controller calls in once the interface is on screen.

Plus a belt in `UpdateDarkUI()`: a *background* of pure black is never
something Orca chose, so it is answered with the page rather than with the
text tone.

Worth stating as a rule, because this cost two rounds: **a colour stored at
construction is only as good as the appearance was at that moment**, and on
iOS the appearance is not knowable that early unless you ask something that
owns a window.

**And then it crashed on launch, which was this patch's doing.** `pc = 0`,
`EXC_BAD_ACCESS`, killed three seconds after start, in
`GLTexture::load_from_svg` under
`GLCanvas3D::_init_select_plate_toolbar()` under
`Plater::priv::on_change_color_mode(SimpleEvent&)` - the handler for the
`EVT_GLCANVAS_COLOR_MODE_CHANGED` this patch posts.

`_init_select_plate_toolbar()` runs *unconditionally* in
`GLCanvas3D::on_change_color_mode()` - it is not under the `reinit` flag - and
it loads the plate toolbar's SVGs into GL textures. Reached before the canvas
has ever been made current, `glGenTextures` and the rest are still the null
pointers glad starts with, and the call lands on address 0.

The sibling handler `on_apple_change_color_mode()` has always guarded exactly
this with `is_initialized()`. The one that took our event did not, because
until now the only thing that posted it was the Preferences dialog, which
cannot be open before the canvas is live. The appearance changing can be, and
after the previous commit it reliably *was*: the walk that repairs stored
colours is now guaranteed to happen once at startup, so the event went out
early for the first time. Both halves of that were new in the same commit,
which is why the crash arrived with it and not with the palette.

The guard is on the handler, per canvas, and skipping loses nothing:
`GLCanvas3D::init()` reads `dark_color_mode` back out of the config, so a
canvas that comes up afterwards comes up in the right appearance anyway.

Worth keeping: **posting an event from a lifecycle callback puts it in a place
its handler has never run from.** Everything reachable from that handler now
has to be safe at that point too, and "reachable" includes GL.

**Third attempt, and this one is arithmetic rather than theory.** The pale
boxes survived both earlier fixes. The reason is `GetParentBackgroundColor()`
again, but its *other* branch - the one for a `StaticBox` with a gradient:

```
average(background_color, background_color2)
```

Every `Label` stores that answer as its own background for life, and **the
average of two palette colours is not itself a palette colour**, so nothing
can translate it afterwards. With this palette,
`average(#FFFEFB, #EDEAE0) = #F6F4ED` - in no table, in either direction, for
ever. Upstream never notices because averaging *its* two greys, `#F8F8F8` and
`#F1F1F1`, lands exactly on `#F4F4F4`, which happens to be a key in its dark
table. It was luck, and the palette spent it.

On iOS the gradient's top colour is returned instead. It is a palette colour,
so it translates in both directions, and it reads the same to the eye because
the two ends are one step apart by design.

That also explains why the boxes were *pale* rather than dark: the average was
taken while `gDarkMode` was still false. `orca_ios_system_is_dark()` was asking
a `UIWindowScene`, which does not exist yet when `init_label_colours()` runs -
the main frame is still being built, no window has been created. It now asks
`[UIScreen mainScreen].traitCollection` in between, which is the source
`wxSystemAppearance::IsDark()` uses on this port; the proof that it answers
honestly that early is that the 3D view has always come up dark, and its mode
comes from `dark_color_mode`, which `on_init_inner()` writes from exactly that
call.

Two fixes for one symptom on purpose: the screen makes the colour right when
it is stored, and dropping the average makes it repairable if it is ever
stored wrong again.

**The gallery was the one surface on screen that belonged to no palette.**
Reported from the device: the home page came up pure black under a charcoal
tab bar. Patch 0430 draws it natively and took `UIColor.systemBackground` for
the page and `secondarySystemBackground` for the cards - which is black and
near-black in dark, white and near-white in light. Correct UIKit, wrong
application.

`orca_ios_page_color()` and `orca_ios_surface_color()` sit beside
`orca_ios_accent()` now, all three built on one `orca_ios_tone(light, dark)`
helper, and the gallery uses them. The values are `kPage` and `kSurface` from
StateColor.cpp, so the native views and the wx ones are the same two tones.

Worth remembering when adding native chrome: **a UIKit system colour is never
this application's colour.** systemBackground is the trap, because it looks
right until you put it next to something that came from the palette.

**The gradients are gone, and with them the last way to make an
untranslatable colour.** Asked for from the device: the Printer, Filament and
Process title bars were two tones a step apart over 30 pixels, which is a seam
rather than a gradient at that size. `SetBackgroundColor2()` is no longer
called for any of the three - `Plater.cpp` twice, `ParamsPanel.cpp` once - so
`StaticBox::render()` takes its flat path and
`GetParentBackgroundColor()` never reaches the averaging branch at all. The
guard added earlier stays as the belt.

**Black digits in the input fields.** Same family, different widget.
`TextInput`'s `text_ctrl` is a real `wxTextCtrl`, so its foreground is a colour
it *keeps*, and `TextInput` sets it in the constructor and then only when the
control is enabled or disabled. A field built while the palette still answered
"light" draws `#262E30` for the rest of the session: black on a dark input.

`TextInput::render()` re-asks now, and the comparison before the set is what
makes that safe - `SetForegroundColour()` invalidates, so an unconditional set
from a paint handler would loop. It costs one colour comparison per repaint
and it is self-correcting whatever the appearance was when the field was
built.

That is the third widget in this file's history to be caught by the same
thing, so as a rule: **a colour a widget stores is a colour that has to be
re-asked somewhere.** The paint path is the honest place, because it is the
only one that runs whatever else went wrong.

**Not verified on device.** The two to look at first are the sidebar in light -
white cards on a warm page is the whole design and it is where Orca's
hardcoded near-whites were densest - and a live appearance switch, which now
has something to switch to for the first time.

## 0428 — the dialog that had nothing left to ask, and the second tile

Two reports from the device, one cause between them: *"wenn ich eine Datei
schliesse kommt immer noch der Datei-speichern-Dialog und wenn ich Ja drücke ist
die Datei zweimal da."*

### The dialog

`close_with_confirm()` asks "The current project has unsaved changes. Would you
like to save before continuing?" on the way into New Project and Open Project.
It is the right question on a desktop, where not saving is a state a project can
actually be in. It is not one here. Since 0424 the backup runs every ten
seconds, the copy in Documents follows it, and leaving the application forces
both — by the time this dialog could appear the work is already on disk twice,
and the two answers lead to the same place.

On iOS it now saves and carries on. That costs no interaction at all, because
`save_project()` on this port does not open a picker: 0367 gives a project with
no path one in `Documents/` instead. A save that fails is logged and the close
proceeds — the backup and the copy both still hold the work, and refusing to
continue would strand the user in a project they cannot leave.

### The second tile, which 0424 only half fixed

0424 stopped *listing* the autosave copy once a project had a file of its own.
That is not the same as not having one, and the reported sequence goes around
it entirely:

1. New project, no path yet. The autosave writes `Documents/Autosave/X.3mf` and
   lists it — correct, it is the only way back to that work.
2. Close. Answer Yes. `save_project()` has no path either, so 0367 sends it to
   `Documents/X.3mf` and lists **that**.

Two entries, same name, different directories, and `wxFileHistory` compares
paths — so nothing upstream can collapse them. `orca_ios_retire_autosave_copy()`
drops the copy's entry and deletes the copy at the moment the project gains a
file. That is not pruning the user's documents: it is this application removing
its own scratch copy, under the name it chose, once the file it was standing in
for exists.

### And the copy should not have existed in the first place

The deeper half. A project that already has a file was still having a *second*
full copy written under `Documents/Autosave` every minute — so the file the user
opens in the Files app was the stale one and the fresh work sat in a directory
they never chose. On a platform whose entire document model is "the file is
always current", the autosave has to *be* the file.

`orca_ios_autosave_target()` now returns the project's own path when it has one
inside our Documents tree — which since 0382 is every project the application
can open, because anything picked from elsewhere is copied into
`Documents/Projects` on the way in. `Documents/Autosave` is left for the one
case that has nowhere else to go: a project that has never been saved.

Writing the project's own file also makes the dirty state honest again —
`up_to_date(true, false)`, `reset_project_dirty_after_save()`,
`update_title_dirty_status()` — so the title and the Save button stop reporting
unsaved changes to a file that is current. Not `up_to_date(true, true)`: that is
the *backup* timestamp and it belongs to the caller that took the backup.

Still `SaveStrategy::Silence` in both cases. Writing the project's own file must
not re-run `set_project_filename()`, which ends in `add_to_recent_projects()`
and a title update — on a timer tick.

### One file per project, now

| the project | autosaved to | on the home page |
|---|---|---|
| never saved | `Documents/Autosave/<name>.3mf` | that copy |
| has a file | its own file, in place | the project itself, once |

The dirty-state clearing has a consequence worth expecting: `close_with_confirm`
will now usually find the project clean and skip its save entirely, because the
autosave got there first. That is the intended shape — the save at close is the
backstop, not the mechanism.

### 0428 and the words nobody outside the source code knows

Feedback from the device, four wordings and one tab.

**"Keinen", next to "Zuletzt geöffnet" on the home page.** Not gettext at all:
`text.js` gives the home page's *Clear all* button and the wizard's
*deselect all filaments* button the **same key**, `t12`, and the German
translated it for the wizard — "Keinen" — which on the home page is nonsense.
0426 splits them: the home button carries its own `orca13` ("Liste leeren"),
`t12` becomes "Alle abwählen" for the two wizard pages that use it. `orca13`
is in the `en` block too, so the other languages fall back to the same English
they show today.

**Shell.** "Top shell layers" was "Obere Schalenschichten". The setting is the
number of solid layers on top, so it says **"Massive Schichten oben"** now, and
"Top minimum shell thickness" — the same setting under its object-list name —
is **"Mindestdicke oben"**. Their tooltips were rewritten to match; four of
them still explained "die obere Schale".

**"Fill Multiline"** is `coInt` 1..10: how many parallel lines each infill line
is drawn as. "Mehrzeilige Füllung" said nothing; it is **"Parallele
Füllungslinien"**.

**"Strength"** is **"Stärke"**, by request.

**0428 — the Multi-device tab is not for Simple mode.** The insert in
`show_device()` is gated on the mode as well as on `is_enable_multi_machine()`,
and `MainFrame::orca_update_multi_device_tab()` adds or removes the page when
the mode changes, called from `GUI_App::update_mode()` next to the other
panels that already react there. Two things it has to respect: the page is
inserted at `tpMultiDevice`, an index that is only correct once the three
pages before it exist — hence the Monitor check, and when they do not exist
`show_device()` is about to build the whole set anyway. And removing the page
while it is the selected one takes the notebook with it, so it steps to the
3D editor first.

## 0231, 0430, 0431 — the menu, the gallery and the drop

Three reports, one shape between them: the parts of this application that a
tablet reaches by touching something were never finished, because wx does not
implement them on iOS and the home page is a web page.

### 0231 — every context menu in the application did nothing

**`wxMenuIPhoneImpl::PopUp()` is `#if 0 // TODO wxIOS` in wxWidgets 3.3.2.**
The whole Cocoa body is commented out and the function returns having shown
nothing, so `wxWindow::PopupMenu()` reports success and no menu appears. That
is every context menu Orca has: the object menu with delete, duplicate, "fill
bed with copies", "export as STL" and the per-object settings; the plate menu;
the filament menu behind the edit button in the sidebar. Patch 0380 taught the
3D canvas to turn a long press into the right click those menus hang off, and
it has been correctly synthesising a right click into a function that draws
nothing ever since.

There is no error and no log line for this, which is why it survived four
months of testing on device: the menu is *asked for* on every long press, and
the answer is silence.

**Why the menu is a `UIAlertController` and not a `UIMenu`.** wx already turns
a wxMenu into a real UIMenu — `wxMenuIPhoneImpl::GetHMenu()` builds the tree
and `wxMenuItemImpl::Create` wires each `UIAction` to
`HandleCommandProcess(peer)`. It cannot be used, because **UIKit has no API
that presents a UIMenu programmatically.** A UIMenu appears only when the
system's own gesture drives it — a `UIButton` with `showsMenuAsPrimaryAction`,
a `UIBarButtonItem`, a `UIContextMenuInteraction` the user has long-pressed.
`PopupMenu()` is a command: *show this menu, at this point, now*. Nothing in
UIMenu answers a command.

`UIAlertController` in its action-sheet style does. It is presentable at an
arbitrary point, and at regular width — which an iPad always is — UIKit draws
it as a popover anchored exactly where the finger was. Long menus scroll,
disabled items are drawn disabled, `wxID_DELETE`/`wxID_REMOVE`/`wxID_CLEAR`
come out red, check items carry a ✓ in the title (there is no checked style on
a `UIAlertAction` and the key that fakes one is private), and a submenu opens
as a second sheet at the same point — dispatched a turn late, because
presenting from a controller that is still dismissing the first sheet is the
"already presenting" exception.

**The invoking window has to be put back.** `wxWindowBase::PopupMenu()` sets
the menu's invoking window for the duration of `DoPopupMenu()` and clears it on
the way out. On every other platform that is the duration of the menu, because
`DoPopupMenu()` runs its own event loop and returns only once an item has been
chosen. Here the sheet is presented and the call returns immediately, so by the
time the handler runs the menu no longer knows which window it belongs to and
`SendEvent()` would deliver the command to nothing. A
`wxMenuInvokingWindowSetter` inside the handler restores it for the send.

A view that wants the blurred UIMenu with icons can still have one by owning a
`UIContextMenuInteraction` of its own — 0430's gallery does exactly that. This
is the floor: there is no longer a menu in this port that does nothing.

### 0430 — the home page is a gallery

The recent projects were `<div class="FileItem">` rows written by
`ShowRecentFileList()` into a WKWebView, with a hand-drawn
`#recnet_context_menu` bound to jQuery's `contextmenu`. On a tablet none of it
is reachable. There is no right click, so the menu has no trigger. There is no
way for a web page to draw the hover state the platform draws for a pointer.
And there is no way to express "these four projects" at all.

`#RightBoard` is hidden (`css/orca-ios.css`) and a `UICollectionView` stands in
its place, in the tab bar controller's view beside the plus and for the same
reason 0400 gives: wx hangs gesture recognisers off every view it wraps, and a
UIKit control buried under those never gets a clean touch.

| what it does | what draws it |
|---|---|
| hover over a tile | `UIHoverStyle` with `UIHoverLiftEffect` — the pointer lifts the tile, nothing here tracks the pointer |
| long press, or a trackpad's secondary click | `UIContextMenuInteraction`, the collection view's own: Open, Share…, Export…, Duplicate, Rename…, Remove from list, Delete |
| Select, the chip left of the plus | selection mode: the tiles become checkboxes and a glass bar rises with Share, Export, Duplicate, Select all, Delete |
| the grid itself | `UICollectionViewCompositionalLayout`, as many columns as fit at 176 pt, reflowing on rotation and in Split View with nothing being told it happened |

**Share and Export are different things and both are here.** Share is
`UIActivityViewController` — AirDrop, Messages, another app. Export is
`UIDocumentPickerViewController` in exporting mode, `asCopy:YES`: put a copy
where I choose, and leave the original where the recent list says it is.

**"Remove from list" and "Delete" are different things too**, and the web
page's menu only ever had the first while calling it *clear* — which is why a
project "deleted" from the home page kept coming back the next time it was
opened. One drops the entry, the other drops the entry *and* unlinks the file,
in that order, because `remove_recent_project()` writes the whole list back to
the app config and an entry pointing at a file that has just been unlinked
would be written out with it.

**Where the data comes from.** `MainFrame::orca_ios_push_gallery()`, called
from `WebViewPanel::SendRecentList` — the one function every path that changes
the recent list already ends in, so the grid and the page can never disagree.
The thumbnails are the bytes `bbs_3mf_get_thumbnail()` already cached in
`FileHistory::m_thumbnails`, handed over as PNG rather than as the base64
`data:` URL the web page needed: going through base64 to decode it again on the
other side is the web page's detour taken for no reason.

The page is deliberately still loaded and still running. The plus calls
`OnClickNewProject()` and `OnClickOpenProject()` on it, and those work whether
or not anything on the page is visible.

### 0431 — a drop, anywhere in the window

On the desktop Orca takes files through `PlaterDropTarget`, a
`wxFileDropTarget` on the 3D canvas. Neither half of that survives the port.

**It is on one view.** A drag on iPadOS comes out of the Files app, out of
Safari, out of another application's share sheet, and it is let go of wherever
the finger happens to be — which on a five-page application is usually not the
plate.

**And wx cannot see it at all.** The iPhone port has no drag-and-drop plumbing:
`wxDropTarget` is not implemented, so a file dragged onto this application
bounced back to where it came from with no indication that anything had been
refused.

So the interaction is a `UIDropInteraction` on the view the tab bar controller
owns — the topmost thing in the window, spanning all of it — and what the drop
*means* is read off the page it landed on:

| where it lands | what happens |
|---|---|
| Prepare, Preview | the model joins the project that is open |
| Home, Device, anything else | it gets a project of its own, and the application goes there to show it |
| **any `.3mf`, anywhere** | a project, always — a 3MF *is* a project, with its plates, its presets and its settings, and merging one into another silently throws all of that away |

Accepted: `3mf stl step stp obj amf oltp svg zip drc ply gcode g` — the same
set as `Plater::load_files`' own regex plus the G-code pair it keeps
separately. Accepting a file the loader will silently drop is worse than
refusing the drag, because the refusal is the only feedback a drag gives.

**The bytes are copied before anything else happens.** What a drop hands over
is a URL into a staging area that is valid for the length of the callback and
no longer, and a project remembers its path (0382). A `.3mf` is copied into
`Documents/Projects` because that is where projects live; everything else into
`Documents/Imports`, so Documents does not fill up with parts that were never
projects. A second copy of the same name is numbered rather than overwriting
the one that may be open.

A drag carrying several files is one action, not several: a `dispatch_group`
waits for every item to load, sorts them the way the Files app showed them,
and hands the whole set over in one `orca_ios_drop_begin/add/commit`.

### What is not verified

None of this has been run. The three to look at first on device are: whether
the action sheet from 0231 comes up under the finger on a long press on the
plate (and whether the item it then runs actually reaches Orca — that is the
invoking-window restore); whether the gallery installs at all, which the log
says outright as `orca-ios-gallery: installed under …`; and whether a drag from
the Files app is accepted on each of the five pages, which the log names as
`orca-ios-drop: N file(s) on page P`.

## 0432 — the restore offer the autosave had already answered

From the device: *"Immer wenn ich die App schliesse und wieder öffne kommt 'ein
ungespeichertes Projekt wurde entdeckt' obwohl autosave schon geregelt hat.
Anschliessend habe ich das Projekt doppelt."*

Both halves are the same directory: the one Orca backs up into.

### Why the offer came up on every launch

`Plater::priv`'s `EVT_RESTORE_PROJECT` handler asks `has_restore_data()` about
`app_config`'s `last_backup_dir`, and that function asks two questions:

1. is there a `<backup>/.3mf`, and
2. is the process named in the `lock.txt` beside it gone?

On a desktop the second is true only after a crash, because an ordinary exit
runs `close_with_confirm()` → `set_backup_path("")` → the directory is removed.
On iOS there is no ordinary exit. The system kills the process, so question 2 is
true after **every** session — and 0424 is what made it answerable at all
(before it, `get_process_name()` compared our own path against itself and the
offer could never appear).

That left question 1 doing all of the work, and nothing was ever answering it
honestly. The backup 3MF is written by the ten-second timer whenever the model
is dirty, and *nothing on this port ever removed it* — the autosave wrote the
project's own file right beside it and left the backup sitting there looking
exactly like unrescued work. So the offer came up on every launch, on top of a
home page whose tiles were already showing the very project it was offering to
bring back.

`save_project()` has always removed it: `Slic3r::remove_backup(model, false)`
deletes `<backup>/.3mf` the moment an explicit save has put the same content in
a real file. On this port the autosave *is* the save — 0428 made it write the
project's own file — and it was not keeping that half of the promise.

### Why saying Yes gave the project twice

A restore reinstates the project's identity from `origin.txt`, and `origin.txt`
is written **once**, by the importer, when the file is first loaded, and never
again. A project that has moved since — saved to `Documents/X.3mf` by 0367, or
written to `Documents/Autosave/X.3mf` while it had no file of its own — comes
back under the path it was *opened* from, so `set_project_filename()` puts that
path on the home page beside the tile for the path the project actually lives
at. Two tiles, one name, one project: the same shape as the duplicate 0428
removed, reached through the restore instead of through Save.

### What 0432 does

One rule, in `orca_ios_backup_overtaken_by()`, called from the autosave right
after a successful export:

> `<backup>/.3mf` exists only while there are changes that no file has.

- The stale backup is deleted, synchronously. Not through the backup manager:
  that queues the removal onto its own thread, and the call that matters most is
  the one from `willResignActive`, where there may be no next moment in which
  that thread is scheduled.
- `origin.txt` is rewritten to the file that was just written, rather than
  removed. It is the identity a restore hands back — so a restore now lands in
  the project's own tile — and it is also the archive the importer falls back to
  for anything missing from the backup, where the complete current file is the
  better of the two answers.
- `Model::current_backup_path()` is new because `get_backup_path()` *creates* a
  directory when the model has not needed one, which is right for a caller about
  to write into it and wrong for one running on a timer with nothing to do.

Nothing is given up. The backup timer writes `.3mf` again as soon as the model
moves on, so a crash between two autosaves still has a copy at most ten seconds
old to come back from — which is the case the offer is actually for, and the
only case in which it now appears.

### What is not verified

Not run on device. Two things to watch in the log: `orca-ios-autosave: retired
the backup …/.3mf ok` on every autosave tick that wrote a file, and the absence
of the restore dialog on the launch after a swipe-up close. The first launch
*after installing this build* is expected to offer the restore one last time —
the directory the old build left behind still has its `.3mf`, and answering
"Nein" is both correct and enough to have it removed.
## 0232, 0233, 0234, 0433 — the right click a finger and a pencil never had

Reported as "Rechtsklick geht mit touch oder pencil nicht", with the context
menu as the example. It is not one bug. Three separate things stood between a
finger and a context menu, and each of them is enough on its own to make every
menu in the application unreachable.

### There was no way to produce a right click at all, except on the 3D canvas

`SetupMouseEvent()` in `src/osx/iphone/window.mm` turns
`UIEventButtonMaskSecondary` into `wxEVT_RIGHT_DOWN` (patch 0209). That mask
exists only on a `UITouchTypeIndirectPointer` — a mouse or a trackpad. A finger
and an Apple Pencil are `UITouchTypeDirect` and `UITouchTypeStylus`, they carry
no button mask, and every touch they make is a left click. Nothing else in the
port synthesised a secondary click.

Patch 0380 gave the 3D canvas a long press of its own: `GLCanvas3D` asks for
`wxTOUCH_PRESS_GESTURES` and turns `wxEVT_LONG_PRESS` into a right click by
hand. That is one control. Everything else — the object list with the object,
part, instance and plate menus, the project-file list, every custom widget that
binds `wxEVT_RIGHT_DOWN` — had no trigger whatsoever.

**0233 makes the long press a port feature rather than a control feature.**
`wxWidgetIPhoneImpl::InstallEventHandler()` already hangs a scroll recognizer
and a hover recognizer off every view wx wraps, for the same reason: desktop
pointer behaviour has to be opted into per view on this platform. A
`UILongPressGestureRecognizer` joins them, and half a second on one spot becomes
`wxEVT_RIGHT_DOWN` + `wxEVT_RIGHT_UP` at that point. From there nothing is
iOS-specific any more, because it is the same event the desktop sends:
`wxWindowMac::OnMouseEvent()` turns the down into `wxEVT_CONTEXT_MENU`, the
generic `wxDataViewCtrl` raises `wxEVT_DATAVIEW_ITEM_CONTEXT_MENU` from the up
(`datavgen.cpp`, `if (event.RightUp())`), and Orca's own handlers see what they
were written for.

Four decisions in it are worth keeping:

| decision | why |
|---|---|
| `allowedTouchTypes` = direct + stylus | a pointer has a real secondary button; hijacking its held left button breaks every drag |
| the delegate refuses to *begin* over a text field, a text view, a `WKWebView` or any view carrying a `UIContextMenuInteraction` | those already have a long press — the loupe, the link menu, 0430's gallery. Refusing before recognition matters: a recognizer that begins and then does nothing has already cancelled the touch under it |
| every recognizer in the chain resolves the *deepest* wx window under the point, and a 0.75 s guard lets only the first through | a press over a control deep in a window is seen by that view's recognizer and by every ancestor's; this does not depend on UIKit's arbitration order between views, and two menus for one press cannot happen |
| `EnableTouchEvents(wxTOUCH_PRESS_GESTURES)` stands it down on that view | a window that handles `wxEVT_LONG_PRESS` itself has said what a press means there — this is what keeps 0380's canvas path the only one on the canvas |

`cancelsTouchesInView` stays `YES`, for the reason 0226 gives: the cancelled
touch becomes a clean `LEFT_UP` (0223), so the tap already in progress completes
and selects the row, the object or the plate under the finger — the order a
desktop right click happens in — and no stray release arrives on lift. A press
that fails because the finger moved cancels nothing, so dragging, orbiting and
scrolling are untouched.

### `wxGetMousePosition()` returned (0,0), and the object list checked it

This one had nothing to do with touch: **the object list's context menu could
not open from a mouse either.**

```cpp
// src/osx/iphone/utils.mm, as shipped
void wxGetMousePosition( int* x, int* y ) { if (x) *x = 0; if (y) *y = 0; }
wxMouseState wxGetMouseState() { wxMouseState ms; return ms; }
```

Nothing can tell that apart from a pointer resting in the corner of the screen,
and two things read it on every context menu:

- `ObjectList::get_mouse_position_in_control()` is
  `wxGetMousePosition() - GetScreenPosition()`, and
  `ObjectList::list_manipulation()` opens with `if (mouse_pos.x < 0) return;`.
  The list is never at the screen origin, so that subtraction was always
  negative and the function returned before it could show anything.
- `wxWindowMac::DoPopupMenu()` substitutes this position whenever `PopupMenu()`
  is called without one — which is most of them, `ObjectList::show_context_menu()`
  included. Every such menu was anchored at the top left corner of the screen.

**A third thing read it, and this one was not about menus at all.**
`ObjectList`'s `wxEVT_DATAVIEW_SELECTION_CHANGED` handler opens with

```cpp
if (!GetScreenRect().Contains(wxGetMousePosition())) return;
```

which was false for every selection this port has ever made, so the handler
returned before `selection_changed()` and picking a row in the object list
never reached the 3D scene or the manipulation panel. With the position
answered properly the test means what it means on the desktop - "the pointer is
over this list" - and the handler runs. Expect the object list to start driving
the selection; that is the behaviour it always had everywhere else.

**0232 records where the pointer, the finger or the pencil last was** — in the
touch path, in the hover path and in the long press above, in the screen
coordinates `wxNonOwnedWindowIPhoneImpl::WindowToScreen()` produces so that a
round trip through `ScreenToClient()` survives — and answers both functions from
it. `wxGetMouseState()` reports the real buttons and modifiers with it, which is
what `GLGizmoMeasure` and `FilamentPickerDialog` poll.

### An action sheet outlives the call that raised it

0231 presents the menu as a `UIAlertController` and returns. That is the only
thing UIKit offers, and it quietly breaks the contract every wxWidgets caller is
written against: `PopupMenu()` blocks everywhere else, so a menu built on the
stack of the function that shows it is alive for exactly as long as it is on
screen. Here it is freed before the user's finger comes down, and the action
handlers 0231 installs captured `wxMenu*`, `wxMenuItem*` and `wxWindow*` raw.

**0234 stops holding raw pointers.** The menu and the invoking window are held
as `wxWeakRef` — `wxEvtHandler` derives from `wxTrackable`, so both go null on
destruction — and the item is not tracked at all: it is looked up again by id in
the menu that is still alive, which also catches an item removed from a menu
that survived. A menu destroyed while its sheet is up dismisses the sheet from
`~wxMenuIPhoneImpl()`, because a menu whose items can no longer do anything has
no business staying on screen.

One thing an async menu still cannot do is return the user's choice to the
caller: `wxWindow::GetPopupMenuSelectionFromUser()` shows the menu and reads a
global the handler sets, so on iOS it always answers `wxID_NONE`. Nothing in
this application reaches it — wx uses it in three places, the wxAuiToolBar
overflow chevron (the topbar is not in the frame's sizer on Apple), the
wxAuiNotebook tab list (Orca draws its own tabs, and 0397 replaced them with a
native bar), and the header ctrl's hide-columns menu (needs `wxHD_ALLOW_HIDE`,
which nothing here sets) — but a fourth caller would need a nested event loop
around the sheet, which is the thing 0231 deliberately did not build.

**0433 fixes the one caller in Orca that did this** — `CheckList::ShowMenu()`
built a `wxMenu` on its stack — by making the menu a member rebuilt on each
press. It also stops `Plater::priv::show_right_click_menu()` clearing the popup
position on iOS: that position is read when an item is *chosen* ("Add text",
"Add SVG" place the new volume where the click was), and clearing it on the way
out of a non-blocking `PopupMenu()` cleared it before the user had chosen.

### What this makes reachable

Long press, or press with the pencil, anywhere:

| where | what appears |
|---|---|
| the plate, on an object | object / part / instance menu (0380's canvas path, unchanged) |
| the plate, empty space | plate menu, default menu |
| the object list, on a row's name | the same menus, through `wxEVT_DATAVIEW_ITEM_CONTEXT_MENU` — this is the one that could not open even with a mouse |
| the object list, empty space | default menu |
| the project-file list (`AuxiliaryList`) | its rename/delete menu |
| any widget binding `wxEVT_RIGHT_DOWN` | `CheckList`'s filter menu, `SkipPartCanvas`, the camera button in `StatusPanel`, the colour picker in `Field` |
| a text field, a web page, the home gallery | nothing new — the system's own long press, which is what those should have |

### What is not verified

None of it has been run on device. In order: whether a long press on an object
list row raises the sheet at the finger (that is 0232 and 0233 together — the
sheet appearing in the corner means 0232 did not take, no sheet at all means
0233 did not); whether choosing an item in it reaches Orca (0231's
invoking-window restore, now behind 0234's weak references); and whether a
pencil raises it as a finger does.

## 0235, 0434 — the colours that were snapshots, and the button with nothing in it

Four reports from the same device session, and they turned out to be three
causes, none of which was a colour being *chosen* wrongly.

### The band and the strip that arrived late

**The band above the Device page and the strip beside its tabs are the same
window.** `Tabbook::DoSize` (0413) hands the page a rect inset by the height of
the floating bar, and the vertical strip of tabs takes a column at the leading
edge; everything neither of them covers is the book's own background. So one
`SetBackgroundColour` in `Monitor.cpp` paints both the horizontal band across
the top and the vertical strip down the side — which is why "the spacer should
be the cream colour of that tab" and "the vertical strip beside the left panel
does not switch in dark mode, and does not switch in light mode either" are one
bug rather than two.

That call happens once, when the book is built, in whichever appearance the
application happened to start in. Nothing re-asks. It is the same class of
defect this file has now written down four times: **a colour a widget stores at
construction is only as good as the appearance at that moment, and has to be
re-asked somewhere.** The band and the strip now ask on every paint, and the
tone is the page rather than a surface — the band is the top of the page it
sits above, not something laid on it.

`TabButtonsListCtrl` — the strip of tab buttons itself — had the same snapshot,
and something worse behind it. `TabButtonsListCtrl::OnPaint` opens with
`UpdateDarkUI(this)`, exactly the re-ask that was needed, and it is **declared,
defined and bound to nothing**: no event table, no `Bind`, upstream included.
It has never run. The fix binds a handler that does the background alone rather
than upstream's, whose selection marker — a filled rectangle under every button
— this port does not show.

### Why "after a while" was the honest description

The interface got its colours right eventually, and that eventually was the
tell. `orca_ios_appearance_changed()` is what walks the window tree and repairs
anything built in the other appearance, and the first call was supposed to come
from `viewDidAppear:` on the tab bar controller. But this port defers `OnInit`
off the launch (0208), and wx turns the run loop while the frame is being built,
so **`viewDidAppear:` can arrive while `GUI_App::mainframe` is still null.**
The function does the right thing with that — it applies what it can and
deliberately does not latch, so that a later call finishes the job — but nothing
was making a later call. `traitCollectionDidChange:` and the trait registration
only fire on an actual switch. So after a launch that raced that way, the first
walk was whatever unrelated update happened to come along.

`viewDidLayoutSubviews` now retries it. Layout runs as soon as the frame exists
and is sized, before any of it can be looked at, and the latch makes every call
after the first walk a single trait read.

### The white bar in the printing box, and the print that could not be stopped

Two complaints, two causes, both in the same box.

**The white bar is the progress bar's track.** `ProgressBar` paints
`m_progress_background_colour`, a plain member holding `#E9E9E9` — a colour in
no table, so nothing could have translated it even in principle. On a dark page
that is a bar of light grey. It is in the palette now, as a fill, and the three
colours the widget paints with are asked for at paint rather than kept from the
constructor.

**And the print could not be paused or stopped because those buttons had
nothing in them.** `wxWidgetIPhoneImpl::SetBitmap` is an **empty stub** in
wx's iPhone port, and `wxAnyButton::DoSetBitmap` calls it for every bitmap a
button is ever given. So on this port no `wxButton` has ever shown a bitmap.
That is not a small gap: an icon-only button carries no label — the icon is the
whole control — so `ScalableButton` came out as an empty rounded rect the width
of its own padding. Pause, resume, stop and part-skip sit next to the progress
bar; a row of blank buttons beside a bar reads as more bar, and there is nothing
to aim at.

0235 implements it. `AlwaysOriginal` on the image, because
`UIButtonTypeRoundedRect` is the system button type on anything this port runs
on and a system button tints its image with the tint colour — a multi-coloured
icon would come out as one flat hue, and wx has already rendered the bitmap in
the colours it wants. Note this is also how a bitmap *changes*, not only how
the first one arrives: pause turning into resume, an icon going grey as its
button is disabled, all arrive through the same call.

This is a step-2 patch, so it invalidates the wxWidgets build cache — the first
run after it rebuilds the port.

### What is not verified

None of it has been run on device. In order: whether the band above Kamera /
Steuerung is the page colour from the first frame; whether the vertical strip
follows a switch immediately in both directions; whether the progress bar's
track is a fill rather than white; and whether pause and stop are now icons
that can be pressed. The last of those is the one with reach beyond this
report — every icon-only button in the application went through the same stub.

## 0236, 0435 — the hardware keyboard, which had never worked at all

Reported from the device: *"Cmd C oder Ctrl C sowie Cmd V oder Ctrl V
funktionieren noch nicht"*, with the ask to go through the rest of the
shortcuts as well. The rest of the shortcuts were in the same state, and so was
every key this application has ever been sent: **not one keystroke has ever
reached wx on this port.**

### Why nothing worked, which is four separate holes in one path

UIKit delivers a key press to the first responder and to nothing else. The
iPhone port could not produce a first responder, could not have handled a press
if it had one, and could not have matched a shortcut if it had handled it.

**Nothing could take the focus.** Three functions in `src/osx/iphone/window.mm`,
all of them empty:

| function | what it was | what that cost |
|---|---|---|
| `wxWidgetImpl::FindFocus()` | body commented out, `return nil` | `wxWindow::FindFocus()` answered "nothing" for the life of the process |
| `wxWidgetIPhoneImpl::SetFocus()` | `// TODO`, `return false` | `wxWindow::SetFocus()` moved nothing, ever |
| `canBecomeFirstResponder` | never implemented | UIView's default is NO, so no wx view could be made first responder even on request |

So `-pressesBegan:` was never called on a wx view, and
`wxWidgetIPhoneImpl::keyEvent()` — which exists, and which reads correctly —
has never once run on a device. There is no error and no log line for this,
because from wx's side no key was pressed.

**The events it would have sent were the wrong ones.** `keyEvent()` sent one
event per press and nothing else. The two events every wx shortcut is actually
built on were never generated:

* **`wxEVT_CHAR_HOOK`** is what `wxWindowMac::OSXHandleKeyEvent()` runs the
  accelerator tables against, and it is the only key event that propagates from
  the focused window up to the frame. `MainFrame`'s whole global shortcut block
  — Cmd+N, +O, +S, +Shift+S, +I, +G, +Shift+G, +R, +J, +F, +P/, — is one
  `Bind(wxEVT_CHAR_HOOK, …)`, and it could not fire.
* **`wxEVT_CHAR`** carries the character, and it is what `GLCanvas3D::on_char`
  handles. That is where **copy, cut, paste**, select-all, delete-all, clone,
  undo, redo, the labels toggle, the seven camera views, the arrange and orient
  keys, the gizmo letters, the filament digits and `?` all live.

The order is now the cocoa port's, in a new `DoHandleKeyEvent()`: char hook,
then key down, then char if key down was skipped, so the application behaves
here as it does on macOS. `SetupKeyEvent()` also learned to ask for the
character at all (`-characters` when nothing modifies the key, so typing keeps
its accents and its case; `-charactersIgnoringModifiers` when a shortcut
modifier is held), and to hand out **upper case key codes** — it was handing
out `'a'`..`'z'`, and every handler in every wx application tests against
`'A'`.

**A cancelled press was never registered.** `pressesCancelled:withEvent:` was
spelled `pressesCancelled:withEvent:withEvent:` — a selector naming no method,
so a key held down when the application is interrupted stayed down in wx's
model for good.

**And the menu bar was empty.** `wxMenuIPhoneImpl::MakeRoot()` was `{}`. UIKit
builds the main menu once, from a block on the first turn of the run loop —
before `OnInit` installs a menu bar, which is what patch 0213's null guard is a
record of — and never asks again unless told. Nothing told it, so on iPadOS
there was no File menu, no Edit menu and nothing under a held Command key, for
the life of the process. `[UIMenuSystem.mainSystem setNeedsRebuild]` in
`MakeRoot()` is the missing line. And `wxMenuItemImpl::Create` carried
`// TODO UIKeyCommand`, so even a menu that appeared had no key equivalents:
items with an accelerator are `UIKeyCommand`s now.

### Ctrl works everywhere Cmd does

`wxOSXIPhoneSetModifiers()` is one mapping shared by every input path in
window.mm — touch, pointer, scroll, hover, the synthesised right click and the
keys, which each had their own copy. wxOSX reports Command as `m_controlDown`,
because `CmdDown()`, `wxMOD_CONTROL` and `wxACCEL_CTRL` all name *the shortcut
modifier*, and on this family of ports that is Command.

An iPad takes any USB or Bluetooth keyboard and most of them are not Apple's;
on those the key under the user's hand is Control. **A lone Control is reported
as Command**, so both drive the same shortcut. Control is reported as itself
only when Command is held too, which keeps Cmd+Ctrl+key distinct. The two are
deliberately never set at once for a lone Control:
`wxAcceleratorTable::GetCommand()` matches every modifier exactly, so an event
claiming both would stop matching Ctrl accelerators rather than start.

### Who has the keyboard

Focus is tracked rather than asked for — UIKit has no counterpart to
`-[NSWindow firstResponder]`, and wx already thunks both halves of the
transition. The record is validated against `-isFirstResponder` before it is
handed out, so a non-wx view taking the focus (the gallery, a rename field)
corrects it, and it is cleared in `~wxWidgetIPhoneImpl` so it can never outlive
its view. A `SetFocus()` on a view not yet in a window is held and re-run from
`-didMoveToWindow`, because controls routinely ask for the focus from their own
creation path.

**A press no wx view was offered** — the application has just launched and
nothing is first responder, or a plain UIKit view holds it — travels up the
responder chain to `wxAppDelegate`, the last responder there is, and is routed
to the window wx believes is focused, or to the top level window UIKit calls
key. Presses are marked as they are offered to wx, so one that a view refused
and let travel on is not delivered twice. This is what makes the global
shortcuts work before the user has tapped anything.

### 0435 — undo, redo, and a canvas that has the keyboard

**Cmd/Ctrl+Shift+Z is redo.** Orca only ever had Ctrl+Y, which is the Windows
spelling; Shift+Z is what macOS and iPadOS bind redo to and is what a keyboard
on an iPad reaches for. Both work, in `GLCanvas3D::on_char` and in the
shortcuts dialog.

**Undo and redo now work wherever the focus is.** They live in
`GLCanvas3D::on_char`, so they only fire while the 3D canvas holds the
keyboard — which on a desktop it nearly always does, because the pointer passes
over the plate on the way, and on a tablet often does not. `MainFrame`'s char
hook takes them when the focus is elsewhere, and leaves two cases alone: the
canvas, which has its own handler and runs the key past ImGui first, and a text
editor, where undo means undo the typing.

**`orca_ios_wake_canvas()` gives the canvas the focus**, not just a frame. It
already runs after every selection the native tab bar makes, which is exactly
the moment a user picks Prepare and then reaches for the keyboard without ever
having touched the plate. A focused text field is left alone.

**Four shortcuts that describe a desktop are gated off**: Hide, Minimise and
Full Screen do nothing on an iPad but consume their keys — and Cmd+Ctrl+F would
swallow the sidebar search now that Control counts as Command. Cmd+Q is the
one that mattered: `wxEVT_CLOSE_WINDOW` on the main frame tears the application
down, and an iOS application that exits itself is indistinguishable from one
that crashed.

### What this changes that was not asked for

`SetFocus()` working is a behaviour change everywhere, not only for the
keyboard: a dialog that focuses its text field now really does, and the
software keyboard comes up with it. That is correct, and it is also the first
time it has happened on this port.

Menu items that carry an accelerator become `UIKeyCommand`s, which fire from
the responder chain rather than from the menu. Two guards against what that
could otherwise cost: an accelerator with no modifier (or Shift alone) is
**not** made into one, because a bare-key command captures that key everywhere
including text fields — a menu item on plain Del would stop Del deleting a
character; and Quit is skipped for the reason above. Enabled state is asked for
again at the moment the command runs (`menu->UpdateUI()`), because UIKit took
its copy of the menu tree when it built the bar and never comes back to ask.

Orca's own Cmd+C/V/X/Z/Y menu items are *not* affected: on `__APPLE__` Orca
writes them with `sep = " - "` rather than a tab (its own fix for macOS issue
8152), so wx never builds an accelerator for them and they stay where they have
always been — `GLCanvas3D::on_char`.

Menu elements no longer capture the `wxMenuItem*` in their handler block. UIKit
keeps the tree it was handed, so an element outlives the item it was built for
every time a menu is rebuilt — the same class of bug 0234 fixed for the context
menu sheets. Items are looked up in a registry keyed by the identifier the
element carries and unregistered when they are destroyed.

### What is not verified

None of it has been run. 0236 is a step-2 patch, so the first run after it
rebuilds the wx port. In order of what to try on device:

1. Tap the plate, select an object, **Cmd+C then Cmd+V** — and the same with
   Ctrl on a non-Apple keyboard.
2. **Cmd+Z / Cmd+Shift+Z**, both with the plate focused and straight after
   changing something in the object list.
3. **Cmd+S / Cmd+N / Cmd+O** immediately after launch, before touching
   anything — that is the app-delegate router, and it is the piece with no
   equivalent on any other port.
4. Hold **Command** and see whether the menu bar HUD lists anything at all;
   that is `MakeRoot()`'s rebuild, and if it is empty nothing else about the
   menu half works either.
5. Type into a sidebar field and confirm the characters still arrive, that
   Del still deletes a character, and that the key does not also reach the
   plate.
