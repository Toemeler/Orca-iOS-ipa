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

## ccache is missing on step 4 (open, costs ~65 min a run)

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
